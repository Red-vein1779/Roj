# Roj — Fas 3: Sökningsoptimering

**Status: LÅST.** När detta dokument låses är det
den slutgiltiga kartan för Fas 3. Den ändras bara om en konkret lucka
identifieras — och i så fall uppdateras dokumentet *innan* implementationen
fortsätter. Kartan är kompassen. Fas 1:s nordstjärna var perft; Fas 2:s var
TT-invarianten. Fas 3:s nordstjärna är **SPRT**: varje teknik måste bevisa ett
statistiskt säkerställt Elo-tillskott innan den signeras av. Varje steg har ett
"klart när"-kriterium, och inga steg dras framåt.

---

## 1. Vad Fas 3 är — och inte är

Fas 2 gav en korrekt, deterministisk sökmaskin som söker *hela* trädet till ett
givet djup (modulo alpha-beta). Fas 3 lär den att söka **selektivt**: att lägga
nästan all tid på de varianter som spelar roll och nästan ingen på resten.
Teknikerna (PVS, null move pruning, LMR, futility-familjen, SEE) kan dubbla
eller tredubbla det effektiva sökdjupet — det är den enskilt största
Elo-källan mellan nu och NNUE.

**Ärlig förväntan vid fasens slut:** Roj söker dramatiskt djupare per sekund
och spelar taktiskt betydligt starkare — men är **fortfarande positionellt
blind**. Evalueringen är oförändrat Fas 2:s tunna stomme (material + små
PST:er). Fas 3 gör sökningen till ett precisionsinstrument; instrumentet mäter
fortfarande med en grov linjal. Schackkunskapen kommer i Fas 4–5.

Fas 3 är fortfarande **single-threaded**. Determinism vid fast djup och fast
Hash förblir grunden för `bench` och felsökning. Lazy SMP får inte krypa in.

**Vad Fas 3 inte är:** ingen evalueringsutbyggnad (Fas 4), ingen NNUE (Fas 5),
ingen async stop/infinite-lyssnare (uppskjutet sedan Fas 2, tidigast Fas 7),
ingen lösning på GHI eller EP-capturability (dokumenterade uppföljningar,
oförändrade).

---

## 2. Verifieringsfilosofi — SPRT ersätter oraklet

Detta är den fundamentala skillnaden mot Fas 1–2 och måste förstås innan någon
kod skrivs.

Fas 2:s tekniker var **sökneutrala**: alpha-beta, TT, dragordning ändrar *hur
fort* svaret hittas, aldrig *vilket* svar. Därför fanns orakel-grindar:
alpha-beta == minimax, ID(N) == direkt djup-N, Hash-oberoende. Fas 3:s tekniker
**ändrar avsiktligt sökträdet** — LMR och pruning söker medvetet *mindre* och
returnerar inte samma värde som en full sökning. Det är hela poängen. Alltså:

> **Fas 3 kan inte verifieras mot ett minimax-orakel. Den primära grinden är
> SPRT: varje teknik implementeras bakom en toggle och måste vinna en
> self-play-SPRT till ett beslut mot föregående version innan steget signeras
> av.** Det är exakt därför SPRT-harnessen byggdes i Fas 2, inte Fas 8.

### 2.1 Pensionering av Hash-oberoende-invarianten — låst beslut

Fas 2:s §8-invariant ("rot-score identisk oberoende av `Hash`-storlek") **dör
by design i och med PVS-commiten** (steg 1). När icke-PV-noder tar fulla
TT-värde-cutoffs blir det fail-soft-värde en cutoff kortsluter med beroende av
vad tabellen råkar innehålla — rot-scoren blir legitimt Hash-beroende, av
exakt det skäl phase2.md §8 redan beskrev. phase2.md §8 förutsåg också detta:
Fas 2 var "det enda rena fönstret" att fästa invarianten; i Fas 3 grumlar
beskärningen vattnet. **Ingen får senare "laga" Hash-beroende rot-scorer som
om det vore en bugg — det är dokumenterat korrekt beteende från steg 1 och
framåt.**

Invarianten **ersätts** av följande konjunktion, som gäller genom hela fasen:

1. **Determinism vid fast konfiguration:** samma binär + samma `Hash` + samma
   ställning + samma fasta djup ⇒ identisk rot-score, identiskt nodantal,
   körning efter körning. (Single-threaded är grunden.)
2. **Bench-signaturdisciplin:** `bench`-nodantalet committas vid varje
   funktionell ändring; en oavsiktlig sökändring syns som en ändrad signatur.
3. **Mate-score round-trip i TT** (Fas 2 §4): kvarstår oförändrad och testad.
4. **TT-drags-legalitet:** ett drag hämtat ur TT verifieras lagligt i
   ställningen innan det används (skydd mot nyckelkollisioner — blir viktigare
   ju hårdare TT:n används).
5. **Mate-sviten:** korrekt matt-score OCH matt-drag på forcerade mattar
   (mate-in-1 till -4) — pruning/reduktioner får aldrig kosta en forcerad matt
   i sviten.
6. **Perft grön + sanitizer-rent:** Fas 1:s grind obruten; ASan+UBSan rena på
   sök + qsearch + TT + nya tekniker.

### 2.2 Undantaget: SEE är orakel-verifierbar

SEE (steg 6) är en **ren funktion** — (position, drag) → förväntat
materialutfall av bytessekvensen — och ändrar ingenting i sökningen förrän den
*används*. Själva funktionen grindas därför deterministiskt mot ett eget
brute-force-orakel (spela upp bytessekvensen med make/unmake och minimax:a
materialet) över en svit av konstruerade ställningar, precis som Fas 1:s
magic-tal grindades mot egen ray-tracing. Ingen SPRT för funktionen; SPRT för
dess två *användningar* (steg 7 och 8).

---

## 3. Låsta arkitekturbeslut

1. **Ordning: störst-Elo-först.** PVS/aspiration (struktur) → NMP/LMR (de stora
   beskärarna) → SEE → marginalbeskärarna → singular extensions. NMP och LMR
   är oberoende av SEE och ger mest Elo per arbetstimme; interaktioner fångas
   av att varje senare teknik SPRT-testas mot löpande master.
2. **PVS återinför TT-värde-cutoffs på spelvägen** — och löser därmed Fas 2:s
   uppskjutna beslut exakt som låst i phase2.md §9: **icke-PV-noder**
   (null-window-sökningarna) tar fulla TT-cutoffs; **PV-noder** fortsätter att
   använda TT enbart för dragordning, vilket bevarar den triangulära PV:ns
   integritet. Styrkan lämnas inte längre på bordet.
3. **Bench om-baseline:as i PVS-commiten — djuphöjningen uppskjuten (ändring
   efter steg 1-fynd).** Nodsignaturen om-baseline:as i samma commit som PVS
   (oförändrat), men bench-DJUPET förblir 6 tills vidare. Dokumenterad
   avvikelse: PVS ensam sänkte nodantalet ~13–42 %, men den effektiva
   förgreningsfaktorn (~5–6×/ply) sätts av beskärningsteknikerna, inte av
   PVS — phase2.md §9:s premiss ("djupare bench blir billig med PVS") höll
   bara delvis, och inget djup > 6 uppfyllde ~1–2 s-målet på referensmaskinen
   (d7 ≈ 11 s, d8 ≈ 42 s). Djuphöjningen flyttas till en definierad punkt:
   **omedelbart efter att Block B:s LTC-regression passerat** (dvs. efter
   LMR-avsigneringen); djupet väljs då empiriskt för ~1–2 s väggklocketid och
   signaturen om-baseline:as i samma commit (åtgärdspunkt i §6, Block B:s
   LTC-rad).
4. **En teknik = en toggle = en SPRT.** Varje teknik implementeras bakom en
   **temporär UCI-option** (default PÅ; av-läget återger föregående beteende
   exakt). fastchess A/B-testar samma binär med option på/av. I
   av-signeringscommiten **tas togglen bort** och tekniken görs ovillkorlig —
   inga döda flaggor lever kvar.
5. **STC för SPRT: 10+0.1** (ersätter Fas 2:s 8+0.08 som arbetshäst —
   **flaggad avvikelse** mot phase2.md §10, motiverad: matchar den uppmätta
   Elo-baslinjen och Rojs profil som relativt starkare på längre TC).
   Concurrency 6, `option.Threads=1`, noll time-losses-invarianten kvarstår.
5b. **Fast partiantal + konfidensintervall för mellan- och finnivåerna
   (ändring efter steg 2-fynd).** [0, 10]-nivån ("stor vinnare") är
   **oförändrad**: sekventiell SPRT till LLR-beslut (±2.94). Nivåerna [0, 5]
   och [0, 3] byter metod: **fast 400 partier** (200 par, `-games 2 -repeat`,
   ingen `-sprt`-flagga), därefter beräknas Elo-differensen med
   95 %-konfidensintervall ur slutresultatet. **PASS** = intervallets undre
   gräns > 0. **FAIL** = övre gränsen < 0. **Oavgjort** (intervallet spänner
   över 0) behandlas som FAIL → parkeringspolicyn (beslut 7) tillämpas.
   **Ärlig avvägning, öppet dokumenterad:** ett fast partiantal ger *svagare
   statistisk styrka* än en fullbordad sekventiell SPRT — detta är ett
   medvetet tidsbudget-före-rigor-val (samma ärlighetsprincip som
   GHI-noteringen i phase2.md §9), och det anges uttryckligen i varje
   av-signeringscommit på dessa nivåer.
   **Sänkning 400 → 150 partier (ändring efter steg 2–3-utfallen, gäller
   från och med steg 4):** steg 2:s och steg 3:s faktiska 400-partiersutfall
   (95 %-KI [+3.1, +65.4] respektive [+13.1, +72.4]) visade tydlig
   signifikans med bred marginal långt före 400 partier — de extra partierna
   smalnade bara redan avgörande intervall (samma resonemang som beslut 6:s
   LTC-sänkning). **Ej retroaktivt:** steg 2:s och steg 3:s redan committade
   400-partiers-avsigneringar står oförändrade och öppnas inte om.
   **Ytterligare sänkning 150 → 100 partier (efter Block B-utfallen, gäller
   från och med Block C):** samtliga grindar hittills har givit KI långt
   över noll utan ett enda gränsfall. Det fasta antalet är därför
   **100 partier** (50 par). **Ärlig styrkevarning, öppet dokumenterad:**
   100 partier är märkbart svagare än tidigare nivåer — i synnerhet för
   [0, 3]-nivåns små förväntade effekter kan en verklig men liten regression
   missas (samma ärlighetsprincip som GHI-noteringen i phase2.md §9).
   Historik: 400 (steg 2) → 150 (före steg 4) → 100 (före Block C).
6. **LTC-regressionsgrind efter varje block: 150 partier (historik: 1 000
   ursprungligen → 200 under Block A → 150 före Block C).** 150 partier
   (75 par, `-games 2 -repeat`) vid **30+0.3**, ny master mot blockets
   startversion. Passkriteriet är i sak oförändrat: ingen statistiskt
   säkerställd regression (95 %-intervallet för Elo-differensen täcker 0
   eller ligger helt över). Motivering för 1 000 → 200: Block A:s löpande
   LTC visade att riktningen avgörs långt före 1 000 partier (140 poolade
   partier gav Elo +118.6, KI [+66.1, +176.9]). Motivering för 200 → 150:
   Block A:s och Block B:s 200-partiersgrindar gav KI helt över noll med
   massiv marginal ([+111, +206] resp. [+234, +362]) — inga gränsfall.
   **Ej retroaktivt:** Block A:s och B:s redan körda 200-partiersresultat
   står oförändrade; 150 gäller Block C:s och D:s kommande grindar.
   **Ärlig styrkevarning:** 150 partier vid LTC ger breda intervall
   (~±75 Elo); en liten verklig regression kan passera — accepterat
   tidsbudget-val, samma redovisningsplikt som beslut 5b. Skyddar fortsatt
   mot STC-överanpassning — särskilt relevant för Roj.
7. **Parkeringspolicy (bindande):** misslyckas en SPRT tillåts **ett (1)**
   dokumenterat omtuningsförsök (parameterändringen skrivs ned *innan* den nya
   SPRT:n startar). Misslyckas även det parkeras tekniken med skriftlig
   motivering i §9 och fasen går vidare. Ingen oändlig fiskexpedition.
8. **Singular extensions är villkorad**, inte obligatorisk: byggs endast om
   block A–D är avsignerade och återstående SPRT-budget bedöms räcka; annars
   flyttas den till en dokumenterad Fas 3.5. (Projektkartan kräver "byggs
   sent"; den tillåter båda utfallen.)
9. **Egna tal och formler överallt.** LMR-reduktionstabellen genereras av vår
   egen formel med våra egna konstanter; NMP:s R, futility-marginaler,
   LMP-trösklar och razoring-marginaler är våra egna startvärden som tunas via
   SPRT. Inga publicerade tabeller eller konstantuppsättningar lyfts in.
10. **Block D:s marginaler är dokumenterad teknisk skuld.** RFP, futility, LMP
    och razoring jämför static eval mot marginaler — och static eval är Fas 2:s
    avsiktligt tunna stomme. Marginalerna tunas mot stommen nu och **måste
    om-tunas när Fas 4/5 byter evaluering.** Detta accepteras öppet (samma
    ärlighetsprincip som GHI-noteringen i phase2.md §9).
11. **CI-grinden byggs i steg 0** (GitHub Actions, `ubuntu-latest`): kör
    ASan+UBSan-perft-grinden och `bench`-signaturkontrollen vid varje push.
    Uppskjuten från Fas 2; en fas full av sökändringar är exakt när den
    behövs.

---

## 4. Originalitet — fortsatt arbetsregel

> **Vår kod och vår data är vår; etablerade *tekniker* är fria.**

PVS, aspiration windows, null move pruning, LMR, SEE, futility/LMP/razoring
och singular extensions är alla publicerade, väletablerade tekniker — vi
återimplementerar dem från grunden. Ingen kod, ingen reduktionstabell, inga
konstantuppsättningar kopieras ur någon annan motor. Referensmotorer förblir
enbart motspelare; fastchess och öppningsboken förblir testverktyg/testdata.

---

## 5. Komponenter i Fas 3

- **PVS** — null-window-sökning av icke-PV-drag med re-search vid fail-high;
  TT-värde-cutoffs återinförda i icke-PV-noder.
- **Aspiration windows** — smalt fönster runt föregående iterations score,
  vidgning vid fail-high/low.
- **Check extension** — schackdrag förlängs ett ply (förutsatt att den inte
  redan finns; verifieras i steg 0).
- **Null move pruning** — med zugzwang-vakt, adaptiv R, aldrig två null i rad.
- **LMR** — egen log-baserad reduktionstabell; re-search vid fail-high.
- **SEE** — ren funktion + brute-force-orakel; används i qsearch-beskärning
  och dragordning.
- **Marginalbeskärare** — reverse futility (static null move), futility
  pruning, late move pruning / move-count pruning, razoring.
- **Singular extensions** *(villkorad)* — TT-dragets singularitet testas med
  reducerad null-window-sökning; singulära drag förlängs.
- **Infrastruktur** — CI-grind (steg 0), toggle-mekanik, SPRT-körningar per
  steg, LTC-regressioner per block, slutgauntlet mot Stockfish-ankaret.

---

## 6. Byggordning — 15 steg i sex block

Stegen följer beroendeordningen. Varje SPRT-steg följer samma cykel:
implementera bakom toggle → verifiera invarianterna (§2.1) → kör SPRT till
beslut → vid PASS: ta bort togglen, signera av, committa → nästa steg.
Claude Code sköter all git; varje stegprompt innehåller en explicit
commit-instruktion, och Claude Code redovisar `git log --oneline` +
`git status` efter varje steg.

### Block 0 — Förberedelse

| # | Steg | Klart när |
|---|------|-----------|
| 0 | Inventering + CI-grind | Claude Code har bekräftat i koden: (a) att check extension **inte** finns i huvudsökningen, (b) exakt var PV-läges-flaggan/TT-cutoff-villkoret sitter, (c) att `ucinewgame`/state-reset gör bench deterministisk. GitHub Actions-workflow (`ubuntu-latest`) kör perft-sanitizer-grinden + bygger release + kör `bench` och jämför signaturen — grönt på main. Basversionen taggad (t.ex. `phase2-final`) som SPRT-ankare för steg 1. |

### Block A — Struktur

| # | Steg | Teknik | Klart när |
|---|------|--------|-----------|
| 1 | PVS + TT-cutoffs + bench-rebaseline | PVS | Icke-PV-noder söker null-window med fulla TT-värde-cutoffs; PV-noder oförändrade (TT enbart ordning); re-search-logiken korrekt (fail-high på null-window ⇒ full-window-omsökning); triangulär PV fortsatt laglig, spelbar och komplett; mate-sviten grön; **§2.1-invarianterna gröna**; bench-djup höjt till ~1–2 s väggklocketid och ny signatur committad i **samma commit**; SPRT **PASS [0, 10]**; toggle borttagen i av-signeringscommiten. |
| 2 | Aspiration windows | Aspiration | Fönster runt föregående iterations score; vidgningsschema vid fail-high/low med fallback till fullt fönster; matt-scorer ⇒ fullt fönster direkt; `info`-utskrift korrekt även vid fail-high/low; SPRT **PASS [0, 5]**; toggle borttagen. |
| — | **LTC-regression block A** | | 200 partier 30+0.3, ny master mot `phase2-final`: ingen säkerställd regression. |

### Block B — De stora beskärarna

| # | Steg | Teknik | Klart när |
|---|------|--------|-----------|
| 3 | Check extension | CheckExt | Schackdrag förlängs ett ply; ply-taket (MAX_PLY) respekteras — ingen förlängningsexplosion (seldepth begränsad); mate-sviten grön; SPRT **PASS [0, 5]**; toggle borttagen. |
| 4 | Null move pruning | NMP | Null-drag görs/ångras korrekt (EP-fält nollas, hash uppdateras — verifierat mot from-scratch-hash); zugzwang-vakt (aldrig i schack; sidan har icke-bondmaterial); aldrig två null i rad; adaptiv R (egen formel); overifierade matt-scorer från null-sökningen returneras aldrig som matt (klipps mot beta); SPRT **PASS [0, 10]**; toggle borttagen. |
| 5 | LMR | LMR | Egen log-baserad reduktionstabell (genererad vid start av egen formel, egna konstanter); reducerar aldrig: slag, promotioner, schackdrag, drag som ger schack, killers, TT-draget, drag när sidan står i schack; re-search vid fail-high (reducerat ⇒ fullt djup ⇒ vid behov fullt fönster); mindre reduktion i PV-noder; SPRT **PASS [0, 10]**; toggle borttagen. |
| — | **LTC-regression block B** | | 200 partier 30+0.3, ny master mot block A-mastern: ingen säkerställd regression. **Därefter (§3 beslut 3): bench-djupet höjs — djupet väljs empiriskt för ~1–2 s väggklocketid, och signaturen om-baseline:as i samma commit.** |

### Block C — SEE

| # | Steg | Teknik | Klart när |
|---|------|--------|-----------|
| 6 | SEE-funktionen + orakel | — (ingen SPRT) | Egen SEE (iterativ swap-algoritm över egna attackuppslag, inkl. x-ray genom glidare; kung deltar sist; promotion/EP-hantering definierad; pinnade pjäser ignoreras — dokumenterad accepterad imperfektion); **brute-force-oraklet** (spela upp bytessekvensen med make/unmake, minimax:a materialutfallet) ger identiskt värde över hela den konstruerade testsviten + slumpade ställningar; deterministisk grind grön i CI. |
| 7 | SEE i quiescence | SEEQPrune | Förlorande slag beskärs i qsearch (utom i schack); delta pruning-samspelet definierat och dokumenterat; taktiksviten (sanity-mätaren) ingen kollaps; **fast 100 partier + 95 % CI, undre gräns > 0** (omklassad från [0,10] efter SPRT-fyndet, se §7); toggle borttagen. |
| 8 | SEE i dragordning | SEEOrder | Slag delas i vinnande/jämna (före killers) och förlorande (efter tysta drag) ovanpå MVV-LVA; identisk bästa score vid fast djup med/utan (ordning är sökneutral på fast djup — detta steg har alltså även en determinism-kontroll); SPRT **PASS [0, 5]**; toggle borttagen. |
| — | **LTC-regression block C** | | 150 partier 30+0.3, ny master mot block B-mastern: ingen säkerställd regression. |

### Block D — Marginalbeskärarna

Gemensamma vakter för hela blocket: **aldrig** i schack, **aldrig** i PV-noder
(utom där explicit beslutat), **aldrig** när scoren är i matt-zonen
(|score| ≥ VALUE_MATE_IN_MAX_PLY), egna marginaler dokumenterade i koden.
Marginalerna är teknisk skuld mot Fas 4/5 (§3 beslut 10).

| # | Steg | Teknik | Klart när |
|---|------|--------|-----------|
| 9 | Reverse futility pruning | RFP | Vid lågt djup: static eval ≥ beta + marginal(djup) ⇒ fail-high utan sökning; vakterna ovan; SPRT **PASS [0, 5]**; toggle borttagen. |
| 10 | Futility pruning | Futility | Vid lågt djup: tysta drag hoppas när static eval + marginal(djup) ≤ alpha; aldrig TT-drag/killers vid tveksamhet — exakt dragmängd dokumenteras; SPRT **PASS [0, 5]**; toggle borttagen. |
| 11 | Late move pruning | LMP | Tysta drag bortom tröskel(djup) hoppas vid lågt djup; tröskeltabell egen; SPRT **PASS [0, 3]**; toggle borttagen. |
| 12 | Razoring | Razor | Vid mycket lågt djup och static eval långt under alpha: fall ned i qsearch (med eller utan verifikation — varianten låses i stegprompten); SPRT **PASS [0, 3]**; toggle borttagen. |
| — | **LTC-regression block D** | | 150 partier 30+0.3, ny master mot block C-mastern: ingen säkerställd regression. |

### Block E — Villkorat

| # | Steg | Teknik | Klart när |
|---|------|--------|-----------|
| 13 | Singular extensions *(villkorad, §3 beslut 8)* | SingExt | Bygg-beslutet dokumenterat (budget räcker/räcker inte). Om byggd: TT-drag med tillräckligt djup + LOWER/EXACT-bound testas med reducerad null-window-sökning exklusive TT-draget; singulärt ⇒ förläng; förlängningsbudget kapad (ingen explosion, seldepth bevakad); SPRT **PASS [0, 3]**; toggle borttagen. Om ej byggd: flyttad till Fas 3.5 med motivering i §9. |

### Avslut

| # | Steg | Klart när |
|---|------|-----------|
| 14 | Slutgauntlet + fas-avsignering | Gauntlet mot Stockfish `UCI_LimitStrength` vid **10+0.1 och 30+0.3** (samma metodik som baslinjen ~2000/~2050); ny nivå uppmätt och dokumenterad i detta dokument (mätning, ingen siffergrind); hela §10-DoD:n genomgången punkt för punkt; dokumentet uppdaterat till **Status: KOMPLETT** och committat. |

Steg 0–2 = strukturen. Steg 3–8 = hjärtat. Steg 9–13 = finslipningen.
Steg 14 = mätning och avsignering.

---

## 7. SPRT-spec — sammanfattning

Gemensamt för alla SPRT-körningar: fastchess, self-play (ny toggle-PÅ mot
samma binär toggle-AV, alternativt ny commit mot förra mastern), parade
färgbytta partier (`-games 2 -repeat`), balanserad bok (`8moves_v3.pgn`,
`order=random`), pentanomial (`-report penta=true`), TC **10+0.1**,
concurrency **6**, `option.Threads=1`, **noll time-losses** (annars sänk
concurrency — invariant från Fas 2). Seed: `-srand <N>` — flaggan
`-randomseed` finns **inte** i den versionslåsta fastchess-binären
(steg 1-fynd); konventionen är `-srand 42`, med varierat seed när partier
från flera körningar poolas.

**Två metoder (§3 beslut 5b):**

| Nivå | Tekniker | Metod | Kriterium |
|------|----------|-------|-----------|
| Stor vinnare | PVS, NMP, LMR | Sekventiell SPRT [0, 10], `alpha=0.05 beta=0.05` | LLR når ±2.94 — körs till beslut, ingen förtida avläsning |
| Medel | Aspiration, check extension, SEE-qsearch (omklassad efter Steg 7:s SPRT-fynd — uppmätt effekt ~+10 Elo med CI som spänner noll efter 651 partier; sekventiell [0,10] är per konstruktion långsam exakt vid effekt ≈ elo1), SEE-ordning, RFP, futility | Fast 100 partier + 95 % CI (steg 2–3 kördes med 400; historik 400→150→100) | Undre CI-gräns > 0 = PASS; spänner 0 eller under = FAIL |
| Finjustering | LMP, razoring, singular extensions | Fast 100 partier + 95 % CI | Undre CI-gräns > 0 = PASS; spänner 0 eller under = FAIL |

**Mallkommando — sekventiell nivå (finslipas per steg i stegprompten):**

```
fastchess \
  -engine cmd=./Roj name=Roj_on  option.Hash=16 option.<Toggle>=true \
  -engine cmd=./Roj name=Roj_off option.Hash=16 option.<Toggle>=false \
  -each proto=uci tc=10+0.1 option.Threads=1 \
  -openings file=8moves_v3.pgn format=pgn order=random \
  -rounds 50000 -games 2 -repeat -recover -srand 42 \
  -concurrency 6 -report penta=true \
  -sprt elo0=0 elo1=10 alpha=0.05 beta=0.05 \
  -pgnout out/roj_<teknik>.pgn
```

**Mallkommando — fast-antal-nivå (100 partier, ingen `-sprt`):**

```
fastchess \
  -engine cmd=./Roj name=Roj_on  option.Hash=16 option.<Toggle>=true \
  -engine cmd=./Roj name=Roj_off option.Hash=16 option.<Toggle>=false \
  -each proto=uci tc=10+0.1 option.Threads=1 \
  -openings file=8moves_v3.pgn format=pgn order=random \
  -rounds 50 -games 2 -repeat -recover -srand 42 \
  -concurrency 6 -report penta=true \
  -pgnout out/roj_<teknik>.pgn
```

**LTC-regression (per block):** samma upplägg utan `-sprt`, `tc=30+0.3`,
`-rounds 75 -games 2` (= 150 partier från Block C, §3 beslut 6 som ändrat;
Block A–B kördes med 200), ny master mot blockets startversion.
Pass: 95 %-intervallet för Elo-differensen täcker 0 eller ligger helt över.
Fail: stopp — utredning och åtgärd innan nästa block påbörjas.

**Statistisk hygien:** en mätning i taget på maskinen. Sekventiell nivå:
SPRT:n körs till sitt beslut (ingen förtida avläsning som beslutsgrund);
H0 förkastad = PASS, H1 förkastad = FAIL ⇒ parkeringspolicyn (§3 beslut 7).
Fast-antal-nivå: kriteriet avläses först vid det fasta partiantalet (100
från och med Block C); ett stoppat delresultat får aldrig användas som
beslut. Partier från en avbruten körning med identisk konfiguration får
poolas till det fasta antalet (fast-N är inte sekventiellt), med varierat
seed för tilläggsbatchen.

---

## 8. Subtiliteterna — exakt

Fas 3:s motsvarighet till Fas 1:s rockadvillkor och Fas 2:s mate-justering —
detaljerna där verkliga buggar bor.

- **PVS-re-search-kaskaden.** Null-window fail-high i en PV-nod ⇒ omsökning
  med fullt fönster på fullt djup. I kombination med LMR (steg 5) blir stegen:
  reducerat null-window → fail-high ⇒ ofullt-reducerat null-window →
  fail-high ⇒ fullt fönster. Ordningen och villkoren skrivs ut i koden.
- **PV-noder och TT förblir som Fas 2 låste dem:** ordning ja, värde-cutoffs
  nej. Det som ändras i steg 1 är enbart icke-PV-vägen. Den triangulära PV:n
  ska vara komplett och laglig efter varje iteration — trunkerad PV är en bugg.
- **NMP och hash.** Null-draget byter sida, nollar EP-fältet och uppdaterar
  Zobrist inkrementellt; from-scratch-hash-invarianten från Fas 1 återanvänds
  som test på null-make/null-unmake. TT-lagring efter null-sökning sker med
  korrekt bound.
- **NMP och matt.** En fail-high från null-sökningen med score i matt-zonen
  bevisar ingen matt (motståndaren fick inte sitt drag) — returvärdet klipps
  under matt-zonen (t.ex. till beta). Annars förgiftas TT:n med falska mattar.
- **Zugzwang-vakten.** NMP av när sidan att dra saknar icke-bondmaterial och
  när sidan står i schack. Detta är minimivakten; verifikationssökning är en
  möjlig senare förfining (parkeras som anteckning, inte krav).
- **LMR-tabellen.** Genereras vid uppstart av egen formel
  (`R = f(djup, dragnummer)`, log-baserad, egna konstanter, avrundning
  definierad). Tabellen är vår — ingen publicerad uppsättning. Reduktionen
  kapas så att djupet aldrig går under 1 in i qsearch på fel väg.
- **Förlängnings-/reduktionsbudget.** Check extension + (ev.) singular
  extensions får aldrig driva ply förbi MAX_PLY; seldepth bevakas i `info`.
  En förlängning per nod (ingen stapling i Fas 3).
- **Static eval-cache.** Block D anropar static eval i noder som förr aldrig
  evaluerade. Evalueras en gång per nod och återanvänds (fält i sökstacken);
  ingen dubbel-evaluering. (Att även cacha eval i TT är en möjlig förfining —
  beslutas i steg 9-prompten, inte nu.)
- **Matt-zons-vakten i all pruning.** RFP/futility/LMP/razoring får aldrig
  agera när alpha eller beta ligger i matt-zonen
  (|värde| ≥ VALUE_MATE_IN_MAX_PLY) — annars beskärs forcerade mattar bort.
- **SEE:s definierade förenklingar.** Pinnade pjäser deltar som om opinnade
  (dokumenterad imperfektion — fullständig pinnhantering i SEE är inte värd
  komplexiteten i Fas 3); x-ray genom egna och motståndarens glidare hanteras;
  kungen deltar sist och bara om motståndaren inte kan återta.
- **Aspiration och matt.** Score i matt-zonen efter en iteration ⇒ nästa
  iteration öppnar med fullt fönster (aspiration runt en matt-score är
  meningslös och buggbenägen). Upprepade fail-high/low ⇒ exponentiell vidgning
  med fallback till fullt fönster.
- **Repetition/GHI oförändrat.** Fas 2:s linje gäller: repetition längs
  sökvägen kontrolleras före TT-konsultation; GHI-imperfektionen accepteras
  och omprövas på TCEC-nivå.
- **TT-drags-legalitet (§2.1 punkt 4 — löst i steg 1-inventeringen).**
  Strukturellt garanterad i dagens sökning: TT-draget används enbart som
  ordningsnyckel och matchas mot en färskt genererad laglig draglista;
  värde-cutoffs exekverar aldrig draget. En nyckelkollision kan därför på sin
  höjd förvränga dragordningen — aldrig injicera ett olagligt drag.
  **Dokumenterad vakt:** OM ett senare steg börjar pröva eller spela
  TT-draget före draggenereringen måste en explicit legalitetskontroll läggas
  till i det steget.
- **Toggle-hygien.** Av-läget måste återge föregående beteende **exakt**
  (verifieras med bench-signatur: toggle-AV ⇒ gamla signaturen). Annars mäter
  SPRT:n fel sak.
- **Interna testlagringsflaggor för osunda tekniker (mönster etablerat i
  Steg 4, godkänt av arkitekten):** tekniker som PVS eller check extension
  kan spegla sitt av-läge i minimax-oraklet (värde-exakt resp. oraklet
  tillämpar samma regel). NMP, LMR och liknande avsiktligt osunda
  beskärningar kan det inte — det finns inget meningsfullt orakel-av-läge
  att jämföra mot. För dessa behålls en intern SearchInfo/build-flagga
  (t.ex. use_nullmove, default false i testkontext) EFTER att UCI-optionen
  och spelvägens villkorsgren är helt borttagna vid av-signering. Detta är
  en testinfrastrukturflagga, inte en kvarlämnad produktionsflagga —
  spelvägen är redan ovillkorlig. Samma mönster gäller för Steg 5 (LMR)
  och andra osunda tekniker i denna fas.

---

## 9. Uppskjutet, parkerat och icke-mål

- **Ärvda uppföljningar (oförändrade från Fas 2 §9):** GHI;
  capturability-medveten EP-repetitionsnyckel (parallell hash); asynkron
  stop/infinite-lyssnartråd (tidigast kring Lazy SMP/Fas 7).
- **NMP-verifikationssökning:** anteckning för framtida förfining, inte
  Fas 3-krav.
- **Eval-cache i TT:** beslutas i steg 9-prompten.
- **Singular extensions:** villkorad — utfall dokumenteras här vid steg 13.
- **Parkerade tekniker:** *(fylls i löpande — teknik, SPRT-utfall,
  omtuningsförsökets parameter, motivering för parkering.)*
  - **Steg 7 (SEE-qsearch-beskärning): PARKERAD.** Basvariant `see < 0`:
    FAIL — 651 partier (avbruten sekventiell körning, adjudicerad under det
    omklassade fast-N-kriteriet), Elo +9.77 ± 24.35, 95 %-KI [−14.6, +34.1]
    spänner noll, noll tidsförluster. Omtuningsförsök (§3.7, parametern
    nedskriven i förväg): `see < −100` (behåll jämna/nära-jämna byten):
    FAIL — 100 partier (srand 43), W39-L39-D22, Elo ±0.00 ± 56.65, KI
    spänner noll, noll tidsförluster. Motivering: Fas 2:s delta pruning
    fångar redan större delen av vinsten; SEE-beskärningens marginella
    värde i qsearch är för litet för att säkerställas. Kodvägen borttagen;
    **SEE-funktionen (steg 6) kvarstår orörd**. Kan omprövas efter Fas 4/5:s
    evalbyte.
  - **Steg 8 (SEE-dragordning): PARKERAD.** Basvariant (förlorande fångster
    under alla tysta drag): FAIL — 100 partier (srand 44), W34-L38-D28,
    Elo −13.90 ± 55.75, KI spänner noll. Omtuningsförsök (§3.7, godkänt och
    nedskrivet i förväg): mjuk nedflyttning (förlorande fångster mellan
    killers och tysta drag, `LOSING_CAPTURE_BASE = 1<<17`): FAIL —
    100 partier (srand 45), W36-L37-D27, Elo −3.47 ± 57.07, KI spänner
    noll. Noll tidsförluster i båda. Motivering: LMR:s indexberoende
    reduktioner läser ordningen (nedflyttade fångster/upplyfta tysta drag
    byter reduktionszon), och MVV-LVA + killers + history ordnar redan
    tillräckligt väl — neutralitetsanalysen visade att ordningskoden i sig
    var värderen (0/14 avvikelser i ren kärna, −16,7 % noder där), så
    parkeringen är en effektfråga, inte en korrekthetsfråga. **Omprövas om
    LMR:s reduktionsindexering ändras eller efter Fas 4/5:s evalbyte.**
    SEE-funktionen (steg 6) kvarstår orörd i kodbasen, orakel-grindad i CI,
    redo för framtida användningar.
- **Block D-marginalerna:** om-tunas efter Fas 4/5-evalbytet (teknisk skuld,
  §3 beslut 10).

---

## 10. Definition of Done — Fas 3

Vi är klara — och får först då röra Fas 4 — när **samtliga** är sanna:

- [ ] **Alla icke-parkerade steg SPRT-avsignerade** till beslut vid 10+0.1,
      noll time-losses, med dokumenterade LLR-utfall.
- [ ] **Alla fyra LTC-regressionsgrindarna passerade** (30+0.3, ingen
      säkerställd regression; block A–B med 200 partier vardera, block C–D
      med 150 vardera per §3 beslut 6 som ändrat).
- [ ] **Parkerade tekniker (om några) dokumenterade i §9** med utfall och
      motivering; singular extensions-beslutet dokumenterat.
- [ ] **SEE-oraklet grönt** över hela testsviten, deterministiskt, i CI.
- [ ] **Perft fortsatt grön** — Fas 1:s grind obruten.
- [ ] **Sanitizer-rent** (ASan + UBSan) på sökningen med alla Fas 3-tekniker
      aktiva, till rimligt djup/tid.
- [ ] **§2.1-invarianterna gröna:** determinism vid fast Hash/djup;
      bench-signatur aktuell och committad; mate-round-trip; TT-drags-legalitet;
      mate-sviten (score + drag).
- [ ] **Inga toggles kvar i koden** — alla tekniker ovillkorliga; inga döda
      flaggor eller UCI-optioner utöver `Hash`.
- [ ] **CI-grinden grön på main** (perft-sanitizer + bench-signatur).
- [ ] **Slutgauntleten körd och dokumenterad** mot Stockfish
      `UCI_LimitStrength` vid 10+0.1 och 30+0.3; ny nivå noterad mot baslinjen
      ~2000/~2050 (mätning, ingen siffergrind).
- [ ] Kompilerar rent med `g++ -O3 -std=c++17 src/*.cpp -o Roj` under
      `-Wall -Wextra -Wpedantic` på g++ 13 (Linux) och 15 (Windows).
- [ ] Ingen rad kod kopierad eller härledd från någon annan engine; alla
      tabeller/konstanter egna.

---

## 11. Bygg-, test- och felsökningskommandon

**Kanoniskt bygge (release):**

```
g++ -O3 -std=c++17 src/*.cpp -o Roj
```

**Sanitizer-byggen:** oförändrade från Fas 2 §12 (motorn `src/*.cpp` med
ASan+UBSan; perft-drivrutinen `tests/perft_sanitize.cpp` enligt phase1.md §9).
Båda körs i CI på `ubuntu-latest` från steg 0.

**`bench`:** nytt djup och ny signatur fastställs i steg 1 (PVS-commiten) och
committas vid varje funktionell ändring därefter — Stockfish-stil, oförändrad
disciplin.

**SPRT och LTC-regression:** se §7.

**UCI-compliance:** `fastchess --compliance ./Roj` körs om efter steg 1
(nya optioner/`info`-flöden) och inför steg 14.

---

## 12. Commit-disciplin

- Claude Code sköter **all** git — Rond kör aldrig git manuellt.
- Varje stegprompt innehåller en explicit commit-instruktion med meddelande
  ("you commit this, not me"); Claude Code redovisar `git log --oneline` +
  `git status` efter varje steg.
- **Av-signeringscommiten per SPRT-steg** innehåller: toggle borttagen,
  bench-signatur uppdaterad, SPRT-utfallet (LLR, W/D/L, pentanomial) i
  commit-meddelandet eller i `docs/`.
- Detta dokument committas som `docs/phase3.md` **innan** steg 0 påbörjas,
  och uppdateras (av Claude Code, på arkitektens instruktion) när parkeringar
  eller mätresultat ska föras in.
