# Roj — Fas 2: Kärnsökning

**Status: LÅST.** Detta är den slutgiltiga kartan för Fas 2. Den ändras bara
om en konkret lucka identifieras — och i så fall uppdateras dokumentet *innan*
implementationen fortsätter. Kartan är kompassen. Till skillnad från Fas 1, där
kompassen var en enda siffra (perft), är Fas 2:s nordstjärna **TT-invarianten**
(§7) plus en konjunktion av objektiva grindar (§10). Varje steg har ett
"klart när"-kriterium, och inga steg dras framåt.

---

## 1. Vad Fas 2 är — och inte är

Fas 1 gav en motor som kan reglerna *perfekt* men inte tänker. Fas 2 ger den
förmågan att *välja* drag: den söker ett träd av varianter och spelar det drag
vars minimax-värde är bäst till ett givet djup.

**Ärlig förväntan vid fasens slut:** Roj spelar taktiskt vettigt — ser och
undviker hängande pjäser, hittar korta forcerade mattar, vinner material den
kan vinna — men är **positionellt blind**. Evalueringen är en avsiktligt minimal
stomme (material + en liten piece-square table), så motorn förstår inte
bondestruktur, kungssäkerhet eller långsiktiga planer. Den slår nybörjare och
svaga motorer; varje riktig motor krossar den. Det är meningen: Fas 2 bygger
*sökmaskinen*, inte schackkunskapen (den kommer i Fas 4–5).

Fas 2 är fortfarande **single-threaded**. Determinism är hela grunden för både
`bench` och TT-invarianten, så ingen `Threads`-option exponeras. Lazy SMP är en
mycket senare fas och får inte krypa in här.

**Den enda erkända avvikelsen mot projektkartan:** sökning kan inte testas utan
en evaluering, men evalueringen ligger nominellt i Fas 4. En alpha-beta-sökning
returnerar minimax-värdet av lövnoderna, och utan *någon* eval finns inget att
returnera — sökningen blir meningslös och, värre, omätbar (SPRT kräver att två
versioner faktiskt spelar olika). Lösningen är en medveten, minimal eval-stomme
(§2). Detta är oundvikligt; allt annat i Fas 2 ligger där kartan säger.

---

## 2. Eval-stommen — slängbar, oberoende av Fas 4-beslutet

Fas 2 inför en **avsiktligt minimal eval-stomme**: material + en liten
piece-square table, utvärderad ur draggande sidans perspektiv. Dess enda uppgift
är att göra sökningen testbar och mätbar. Den är så tunn att den **inte**
föregriper det strategiska Fas 4-beslutet (slängbar bootstrap kontra permanent
stillager), som är **medvetet uppskjutet**.

> **Låst:** Fas 2-stommen är **slängbar oavsett vad Fas 4-beslutet senare blir.**
> Att stommen är slängbar är en separat sak från Fas 4-beslutet och binder det
> inte. När Fas 4 byggs ersätts eller byggs stommen ut enligt det beslut som då
> fattas.

**Krav på stommen:** den måste vara **symmetrisk** — `eval(pos)` ur draggande
sidans vy ska vara exakt `-eval(pos)` för samma ställning med färgerna speglade.
En osymmetrisk eval ger subtila sökfel som är svåra att spåra. Symmetrin
verifieras i steg 1.

Materialvärden (våra, i en centipawn-liknande enhet; magnituderna är standard,
koden är vår): `P=100, N=320, B=330, R=500, Q=900`. PST:erna är små och hålls
medvetet enkla.

---

## 3. Låsta arkitekturbeslut

1. **Fail-soft negamax alpha-beta.** Vi returnerar det *faktiska* bästa värdet
   även när det faller utanför fönstret `[alpha, beta]`, i stället för att klippa
   till fönsterkanten (fail-hard). Skälet: fail-soft ger mer information till
   transpositionstabellen och till aspiration windows i Fas 3.
2. **Eval-stomme: material + liten PST, slängbar** (se §2).
3. **Quiescence search är fundamental, inte en optimering.** I lövnoderna
   fortsätter vi söka tills ställningen är "tyst": slag och dampromotioner, och —
   *om vi själva står i schack* — alla flyktdrag. Stand-pat som nedre gräns +
   delta pruning. **Ingen SEE i Fas 2** (SEE hör till Fas 3); stand-pat + delta +
   MVV-LVA räcker för att hålla qsearch i schack.
4. **Dragordning:** TT-drag → MVV-LVA (slag) → killer moves (två per ply) →
   history heuristic (tysta drag).
5. **Transposition table:** använder Zobrist-hashen från Fas 1; lagrar
   bound-typ (exact / lower / upper), djup, bästa drag och justerad score.
   **Mate-score-justering per ply** (§4, §9). Ersättningsschema:
   **depth-preferred + always-replace** i två fack per index. UCI-option `Hash`,
   default 16 MB.
6. **PV via triangulär tabell** — pålitligare för korrekt `info`-utskrift än att
   extrahera PV ur TT (TT-rader kan skrivas över mitt i en sökning).
7. **Remi = 0; contempt uppskjutet.** Tvåfaldig repetition i sökträdet räknas som
   remi, plus pre-rot-historiken; 50-dragsregeln; otillräckligt material.
   Contempt är en spak som hör ihop med stilsystemet (Fas 6) och tuning (Fas 8).
8. **Enkel tidshantering här** — mjuk/hård gräns, tidskoll var N:te nod, ren
   abort som returnerar bästa draget från senast *avslutade* iterationen. Inte
   uppskjuten till Fas 7; den sofistikerade versionen är det.
9. **Score-konventionerna spikas nu** (§4). TT-lagring och `info score mate N`
   hänger på dem — precis som EP-konventionen spikades i Fas 1 för att inte ge
   falsklarm.

---

## 4. Score-konventioner — spikade

Detta är Fas 2:s motsvarighet till Fas 1:s noggrant spikade EP-konvention: en
detalj som måste vara bestämd *innan* koden skrivs, annars larmar TT-invarianten
falskt eller `info score` blir fel.

- **Beräkning i `int`** (32-bitars) under sökningen; **TT lagrar score som
  `int16_t`** för att spara plats. Alla sentinelvärden ligger med marginal inom
  `int16_t` (≤ 32767).
- `VALUE_DRAW = 0`.
- `VALUE_MATE = 32000`. Matt som *vi* sätter om N halvdrag scoras
  `VALUE_MATE - ply` (närmare matt ger högre score). Matt *mot* oss scoras
  `-VALUE_MATE + ply`.
- `VALUE_INFINITE = 32001` — strikt över varje verklig score; används som
  initialt alpha/beta. (Materialevalens maxbelopp är några tusen cp, långt under
  matt-scorerna, så inget krockar.)
- `VALUE_MATE_IN_MAX_PLY = VALUE_MATE - MAX_PLY` och
  `VALUE_MATED_IN_MAX_PLY = -VALUE_MATE + MAX_PLY` är trösklarna som skiljer
  matt-scorer från normala scorer. `MAX_PLY` låses (t.ex. 246).
- **TT mate-justering (exakt regel, i ord):**
  - *Vid lagring* `value_to_tt(v, ply)`: om `v >= VALUE_MATE_IN_MAX_PLY`, lagra
    `v + ply`; om `v <= VALUE_MATED_IN_MAX_PLY`, lagra `v - ply`; annars `v`.
  - *Vid läsning* `value_from_tt(v, ply)`: om `v >= VALUE_MATE_IN_MAX_PLY`,
    returnera `v - ply`; om `v <= VALUE_MATED_IN_MAX_PLY`, returnera `v + ply`;
    annars `v`.
  - Skälet: en matt-score lagras relativt *noden* men måste tolkas relativt
    *roten*. Round-trip-testet (lagra på ett ply, läs på ett annat, kontrollera
    avståndet) hör i Definition of Done.

---

## 5. Originalitet — fortsatt arbetsregel

> **Vår kod och vår data är vår; etablerade *tekniker* är fria.**

Samma linje som genom hela projektet. Alpha-beta, transpositionstabeller,
MVV-LVA, killer/history, quiescence search är välkända *tekniker* — vi
återimplementerar dem från grunden, vi kopierar ingen kod.

**Testinfrastrukturen rör inte originaliteten.** Öppningsböcker, referensmotorer
som *motspelare* och fastchess är testdata och verktyg, inte motorkod. Vi kopierar
ingen rad kod ur någon motor; motståndarmotorer spelar bara *emot* oss. Detta är
samma princip som perft-facit och Chess-Programming-Wiki-värdena i Fas 1.

---

## 6. Komponenter i Fas 2

- **Söknings­kärna** — fail-soft negamax med alpha-beta, iterativ fördjupning,
  triangulär PV-tabell.
- **Quiescence search** — slag + dampromotioner + schackflykt, stand-pat,
  delta pruning. Använder Fas 1:s separata slagdragsgränssnitt.
- **Dragordning** — TT-drag, MVV-LVA, killers (två per ply), history.
- **Transposition table** — Zobrist-nyckel, bound-typer, mate-justering,
  depth-preferred + always-replace, `Hash`-option.
- **Remi-detektion i sökningen** — tvåfaldig repetition (träd + pre-rot),
  50-drag, otillräckligt material.
- **Eval-stomme** — material + liten PST (slängbar).
- **Enkel tidshantering** — tidsbudget ur `go`-parametrarna, mjuk/hård gräns,
  avbrytbar sökning, ren abort.
- **`bench`-kommando** — deterministisk nodsignatur (parallellt infrastrukturspår).
- **UCI-utökningar** — full `info` (depth/seldepth/score/nodes/nps/time/pv),
  `Hash`-option, hantering av tidsparametrar i `go`.
- **SPRT-harness (extern)** — fastchess; parallellt infrastrukturspår (§9).

---

## 7. Byggordning — 11 steg

Stegen följer beroendeordningen; varje vilar på de föregående. Vid problem är
en grund, deterministisk testställning + nodantal nästan alltid vägen in.

| # | Steg | Klart när |
|---|------|-----------|
| 1 | Score-konventioner + eval-stomme | Konstanterna i §4 låsta; material+PST utvärderar; `eval(pos) == -eval(spegel)` verifierat; mate-konventionen dokumenterad i koden. |
| 2 | Negamax alpha-beta, fast djup | Ingen TT, ingen qsearch. Hittar mate-in-1 och mate-in-2 i testställningar; rot-score matchar handräknat på grunda träd; fail-soft-returen korrekt. |
| 3 | MVV-LVA-ordning av slagdrag | Slag sorteras högt-värderat-offer / lågt-värderad-angripare först; nodantalet sjunker mot osorterat **med identisk score**. |
| 4 | Quiescence search | Taktiska värden stabiliseras (inga hängande-pjäs-blunders vid horisonten); schack i en qnod genererar *alla* flyktdrag; delta pruning aktiv; **perft fortsatt grön**. |
| 5 | Killers + history (tysta drag) | Nodantalet sjunker vid samma djup med **identisk bästa score** och (i normalfallet, strikt unik score) identiskt bästa drag. |
| 6 | Transposition table | TT-invarianten håller (§7): rot-score identisk med/utan TT och oberoende av `Hash`-storlek; mate-score round-trip korrekt; ersättning fungerar; tripwire bekräftar att invarianten inte är en no-op. |
| 7 | Iterativ fördjupning + PV + `info` | ID rapporterar depth/score/nodes/nps/pv/time korrekt; PV är laglig och spelbar; scoren mellan iterationer är rimlig och monoton i normalfallet. |
| 8 | Remi-detektion i sökningen | Tvåfaldig repetition i trädet ger 0; pre-rot-historiken räknas; 50-drag och otillräckligt material ger 0; matt har företräde framför 50-drag; verifierat på konstruerade ställningar. |
| 9 | Tidshantering + avbrytbar sökning | **Förlorar aldrig på tid** över flera tidskontroller (inkl. mycket korta); ren abort returnerar förra hela iterationens bästa drag; mjuk/hård gräns respekteras. |
| 10 | `bench`-kommando | Deterministiskt; **identiskt nodantal** mellan körningar och mellan Windows/Linux. |
| 11 | SPRT-harness + första self-play-SPRT | fastchess kör en parad SPRT med öppningsbok till ett **beslut** (t.ex. qsearch på/av passerar [0, 10]); pentanomial statistik; **noll time-losses** vid vald TC/concurrency. Referensmotorer valda och versionslåsta. |

Steg 1–2 = stommen. Steg 3–8 = hjärtat. Steg 9–11 = mätbarhet och skal.

---

## 8. TT-invarianten — definitionen av "korrekt sökning"

Perft fungerade i Fas 1 för att det fanns ett oberoende, exakt facit. För
sökning finns ingen enda magisk siffra — men det finns en **stark, objektiv
invariant** som spelar samma roll:

> **I en deterministisk, single-threaded, fail-soft alpha-beta vid fast djup
> måste rot-scoren vara *identisk* med och utan transpositionstabell, och
> oberoende av `Hash`-storleken** (testa t.ex. 1 MB mot 256 MB).

- **Bästa draget** är identiskt när den bästa scoren är *strikt* bättre än
  alla alternativ (normalfallet). Vid exakt lika score kan draget skilja sig —
  det är **inte** en bugg.
- **Mate-score round-trip** är en del av invarianten: lagra en matt-score på ett
  ply, läs den på ett annat, kontrollera att avståndet är korrekt (§4).
- **Tripwire:** precis som Zobrist-tripwiren i Fas 1 — korrumpera medvetet ett
  TT-fält och bekräfta att invarianten *larmar*. Annars vet vi inte att kontrollen
  faktiskt testar något.

**Varför just i Fas 2:** Fas 3:s LMR, null move pruning och futility pruning
bryter exakt reproducerbarhet-mot-TT på subtila sätt — reduktionerna beror på
dragordning och history, som TT-träffar stör. Fas 2 är därför det **enda rena
fönstret** att bevisa TT:n innan beskärningen grumlar vattnet. Bevisas den inte
nu, bevisas den aldrig.

---

## 9. Subtiliteterna — exakt

Detaljerna som skiljer en riktig plan från en komponentlista — Fas 2:s
motsvarighet till Fas 1:s noggrant spikade kantfall.

- **Mate-score-justering i TT.** Exakt regel i §4. Klassisk subtil bugg om den
  glöms: matt-avstånd blir fel via TT.
- **GHI (graph history interaction).** En ställnings remi-status beror på *vägen*
  dit, men TT:n är *vägoberoende*. Detta kan ge fel remi-score via TT. Pragmatisk
  linje i Fas 2: **detektera repetition längs sökvägen *innan* TT konsulteras för
  remi-syften**, och acceptera viss GHI-imperfektion. **Ärligt:** vi "löser" inte
  GHI i Fas 2. Det är okej nu men spelar roll på TCEC-nivå och tas upp igen senare.
- **Ren abort.** När tiden tar slut mitt i en iteration måste vi returnera bästa
  draget från senast *avslutade* iterationen, aldrig ett halvfärdigt resultat.
  Sökningen kollar en abort-flagga var N:te nod; ID-loopen håller kvar förra hela
  bästa draget.
- **Qsearch och schack.** Står sidan i schack i en qsearch-nod genereras *alla*
  flyktdrag — ingen stand-pat (man kan inte "stå kvar" i schack). Saknas lagliga
  drag i schack → matt (returnera mated-score). Annars stand-pat = statisk eval
  som nedre gräns: `stand_pat >= beta` → returnera `stand_pat` (fail-soft);
  annars höj alpha till `stand_pat`.
- **Delta pruning.** Hoppa över ett slag som ens med marginal inte kan höja alpha
  (`stand_pat + tagen_pjäs + margin < alpha`). **Stängs av** i schack och nära
  promotion (där värdet kan skjuta i höjden).
- **Repetition och pre-rot-historik.** Tvåfaldig repetition i trädet räknas som
  remi (en sökoptimering; inte trefaldig). Pre-rot-historiken — de faktiska
  partistegen via `position startpos moves ...` — måste matas in så att en
  repetition som *började före roten* fångas.
- **EP-konvention.** Den måste vara *exakt samma* som Fas 1:s när repetitionen
  nycklas, annars larmar invarianten falskt (samma anteckning som i phase1.md §9).
- **50-drag mot matt.** Är halvdragsräknaren ≥ 100 men sidan är matt, har matten
  företräde — kontrollera matt före remi-beslutet.
- **Killer/history-hygien.** Två killers per ply, korrekt rensade/åldrade mellan
  sökningar; history-tabellen (indexerad per `[side][from][to]`) halveras/åldras
  så den inte spiller över.

---

## 10. Testinfrastruktur — SPRT, hårdvaruanpassad

Parallellt infrastrukturspår; börjar i Fas 2, inte Fas 8. Hårdvara: **8 fysiska
kärnor / 16 logiska, en maskin, ingen molnbudget.**

- **CI — sanitizer-grind (GitHub Actions, `ubuntu-latest`):** ett workflow kör
  ASan+UBSan-sanitizer-grinden (`tests/perft_sanitize.cpp`, se phase1.md §9) vid
  **varje push** — gör Linux/sanitizer-kontrollen automatisk och permanent och
  ersätter manuella lokala Linux-körningar. WSL är uppskjutet.
- **Verktyg:** fastchess (valt). Pentanomial statistik via `-report penta=true`.
  Den inbyggda UCI-compliance-checkern (`fastchess --compliance <engine>`) används
  som en snabb grind på vår UCI-loop.
- **Concurrency:** starta på **6** (av 8 fysiska kärnor; lämna marginal för OS +
  fastchess-processen), varje motor med `option.Threads=1`. Kliv upp mot 7–8
  *endast* om time-losses är exakt noll. Använd **aldrig** `-force-concurrency`.
  Invariant: noll time-losses, annars sänk concurrency.
- **Tidskontroll:** STC-arbetshäst **8+0.08**; tillfällig LTC-bekräftelse
  **40+0.4 / 60+0.6**. Validera tidshanteringen (steg 9) vid en längre/fast
  inställning *innan* nedväxling till 8+0.08.
- **SPRT-gränser:** **[0, 10]** för stora Elo-vinster och en ännu svag motor
  (qsearch, TT, MVV-LVA); **[0, 5]** för mindre funktioner (history-justeringar);
  **[-5, 0]** non-regression för refaktoreringar. Genomgående
  `alpha=0.05 beta=0.05`; LLR-gränser (-2.94, 2.94).
- **Öppningsbok:** balanserad bok (`8moves_v3.pgn`) för svag motor; parade,
  färgbytta partier (`-repeat` / `-games 2`). Boken är testdata,
  originalitetsneutral.
- **Två ankarspår:** (a) **relativ self-play-SPRT** (version mot föregående) =
  arbetshästen; (b) **absolut ankare** via referensmotorer (enbart motspelare).
- **Referensmotorer (enbart motspelare, ingen kod):**
  - *Primärt justerbart ankare:* **Stockfish** med `UCI_LimitStrength` +
    `UCI_Elo`, kalibrerat mot CCRL 40/4. En binär, ren UCI; vrid upp `UCI_Elo`
    allteftersom Roj stärks. Brasklapp: spelar svagt genom att ibland välja sämre
    drag — dugligt för absolut nivå, inte för naturligt spel eller stil.
  - *Fasta svaga pinnar:* **TSCP** (mycket svag, trivial att bygga, stabil
    sparring) och ev. **Faile** (~1930 CCRL Blitz). Protokoll-notis: dessa är
    typiskt xboard/WinBoard — körs via `proto=xboard` i fastchess (fungerar, men
    inte ren UCI).
  - *Valfri människokalibrerad stege:* **Maia** via lc0 (pinnar ~1100/1500/1900,
    mer naturligt spel) — tyngre uppsättning.
  - Slutligt val + versionslåsning är en deluppgift i **steg 11**; exakta
    ratingsiffror verifieras då, inte här.
- **Exempelkommando (mall, finslipas i steg 11):**

```
fastchess \
  -engine cmd=./Roj_new  name=Roj_new  option.Hash=16 \
  -engine cmd=./Roj_base name=Roj_base option.Hash=16 \
  -each proto=uci tc=8+0.08 option.Threads=1 \
  -openings file=8moves_v3.pgn format=pgn order=random \
  -rounds 50000 -games 2 -repeat -recover -randomseed \
  -concurrency 6 -report penta=true \
  -sprt elo0=0 elo1=10 alpha=0.05 beta=0.05 \
  -pgnout out/roj_qsearch.pgn
```

---

## 11. Definition of Done — Fas 2

Vi är klara — och får först då röra Fas 3 — när **samtliga** är sanna:

- [ ] **TT-invarianten håller** (§7): rot-score identisk med/utan TT och oberoende
      av `Hash`-storlek, över en svit av ställningar och djup.
- [ ] **Mate-svit:** korrekt matt-*score* OCH matt-*drag* på en uppsättning
      forcerade matt (mate-in-1 till -4).
- [ ] **Aldrig förlust på tid** över flera tidskontroller, inklusive mycket korta.
- [ ] **`bench` deterministiskt** — identiskt nodantal mellan körningar och mellan
      Windows och Linux.
- [ ] **Perft fortsatt grön** — Fas 1:s grind är inte bruten av något i Fas 2.
- [ ] **Sanitizer-rent** (ASan + UBSan) på sök + qsearch + TT till rimligt
      djup/tid i debug-bygget.
- [ ] **Score-konventioner och mate-distans** dokumenterade i koden och
      round-trip-testade i TT (§4).
- [ ] **SPRT-harnessen körbar** med minst en avslutad self-play-SPRT som nått ett
      beslut; noll time-losses; referensmotorer valda och versionslåsta.
- [ ] Kompilerar rent med `g++ -O3 -std=c++17 src/*.cpp -o Roj` under
      `-Wall -Wextra -Wpedantic` på g++ 13 (Linux) och 15 (Windows).
- [ ] Ingen rad kod kopierad eller härledd från någon annan engine.

**Sanity-mätare (ej grind):** andel lösta ställningar i en taktiksvit (t.ex.
WAC / Win at Chess) vid fast tid. Ärligt: andelen beror på tid och eval-kvalitet
och är därför ingen perft-grind — men den fångar grova sök-/qsearch-regressioner.

---

## 12. Bygg-, test- och felsökningskommandon

**Kanoniskt bygge (release):**

```
g++ -O3 -std=c++17 src/*.cpp -o Roj
```

Identiskt på Windows (via MSYS2-skalet) och Linux. Inget Windows-specifikt
införs någonsin.

**Debug-/sanitizerbygge — två mål.**

*Motorn* (`src/*.cpp`) — sanerar Fas 2:s nya kod: search/qsearch/TT via `go`.
Detta är det relevanta sanitizer-målet för själva sökningen:

```
g++ -O1 -g -std=c++17 -fsanitize=address,undefined -fno-omit-frame-pointer src/*.cpp -o Roj_debug
```

*Perft-drivrutinen* (`tests/perft_sanitize.cpp`) — Fas 1:s sanitizer-grind för
draggeneration/make-unmake; oförändrad, körs vidare så att perft förblir
sanitizer-ren (fullständigt kommando i phase1.md §9):

```
g++ -O1 -g -std=c++17 -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer tests/perft_sanitize.cpp src/perft.cpp src/movegen.cpp src/position.cpp src/fen.cpp src/attacks.cpp src/magic.cpp src/zobrist.cpp -o /tmp/perft_sanitize
```

Full ASan är ett Linux-steg (MinGW saknar `libasan`); UBSan fångar skift-klassen
även på Windows i trap-läge. Båda målen körs på `ubuntu-latest` i CI (§10). Samma
disciplin som Fas 1.

**`bench`:** kör en fast uppsättning ställningar till ett fast djup och
rapporterar totalt nodantal. Talet committas i git vid varje funktionell ändring
(Stockfish-stil) så att en oavsiktlig ändring av sökningen syns direkt som ett
ändrat nodantal.

**UCI-compliance:** `fastchess --compliance ./Roj` som snabb grind på handskakning
och protokollefterlevnad.

**SPRT:** se §10 för det hårdvaruanpassade upplägget och exempelkommandot.
