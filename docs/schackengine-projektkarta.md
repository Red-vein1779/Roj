# Projektbeskrivning & Arkitekturkarta — Världsklass Schackengine

> **Levande dokument.** Detta är projektets karta, mål och regler samlat. Det uppdateras när luckor identifieras — innan implementation fortsätter. Planen följer alltid den verkliga beroendeordningen: draggeneration före sökning, sökning före optimering, NNUE efter att stark sökning är etablerad.

---

## Vad detta projekt är
Vi bygger en världsklassig schackengine från absolut noll i C++. Målet är att bevisa att ett människa–AI-samarbete kan producera en tävlingskraftig engine utan att människan är en erfaren programmerare. Projektet är open source.

Den unika identiteten ligger i **stilsystemet** — flera igenkännbara personligheter som ingen annan topengine erbjuder. Styrka (Elo) och stil byggs parallellt; de är inte alternativ till varandra.

## Motiv
Att bevisa AI som ett kraftfullt verktyg för avancerad mjukvaruutveckling — erkänt av professionella schackspelare.

## Slutprodukt
En stark, originell schackengine med:
- Flera valbara spelarstilar (attacking, kreativ, solid, dynamisk, positionell och fler) som kan väljas före varje match
- Flexibelt byte av stil och personlighet
- UCI-protokoll (fungerar med alla schack-GUI:er och Lichess)
- Plattformsoberoende — Windows och Linux
- Open source
- Byggd för att tävla: Lichess och CCRL som delmål, **slutmål TCEC Premier Division**

## Tekniska krav
- **Språk:** C++ med `-O3 -std=c++17`
- **Plattform:** plattformsoberoende — aldrig Windows-specifika funktioner. Linux-kompatibilitet inbyggt från dag ett.
- **Originalitet:** ingen kod kopierad eller härledd från andra engines — allt 100 % originellt.
- **Klargörande om originalitet:** att återimplementera en *publicerad teknik eller arkitektur* från grunden (t.ex. ett känt NNUE-feature-set, magic bitboards, Syzygy-probning) är originalarbete och standard praxis. Det förbjudna är att *kopiera kod* — inte att *implementera en känd teknik*. Denna linje gäller genom hela projektet och måste hållas tydlig redan från fas 1, annars riskerar fas 5 och 7 att stanna av i onödan.

---

# Arkitektur — 8 faser

## Fas 1 — Grund ✅ KOMPLETT
Grunden måste vara felfri innan något annat byggs, eftersom en draggenereringsbugg förgiftar allt ovanför den. "Felfri" definieras därför med en konkret metod, inte med en känsla.

**Komponenter:**
- Bitboards (64-bitars heltal för brädrepresentation)
- Förberäknade attacktabeller (springare, kung, bonde)
- **Magic bitboards** för glidande pjäser (torn, löpare, dam) — valt framför PEXT/BMI2 för portabilitet. Magic-talen genereras av motorns *egna* kollisionssökningskod, inte publicerade tabeller (originalitetskrav).
- Brädtillstånd med **Zobrist-hashing inbyggt redan här** — behövs för transpositionstabell och repetitionsdetektion. Att retrofitta inkrementell hashing i en fungerande make/unmake senare är dyrt och felbenäget.
- FEN-parsning
- Dragkodning
- make/unmake med history-stack
- Komplett draggeneration inkl. alla specialdrag (rockad, en passant, promotion) — **pseudo-legal generation + legalitetsfilter** (make-then-test) snarare än fullt-legal generation.
- Perft/divide-verktyg
- Grundläggande UCI-loop

**Definition of Done (icke-förhandlingsbar grind till fas 2):**
1. Perft-resultat matchar kända facitvärden på alla sex standardställningar.
2. Per-nod Zobrist-hash-invariant (from-scratch == inkrementell) i varje perft-nod.
3. Rena sanitizer-körningar (UBSan + AddressSanitizer).
4. Magic-talen genererade av egen kod.

**Status: KLAR OCH VERIFIERAD.** Perft-porten passerad på alla sex standardställningar (~592 miljoner ställningar). Hash-invariant tripwire-bekräftad. ASan+UBSan rena på Linux. Egna magic-tal. Fullständigt dokumenterad i `docs/phase1.md`.

## Fas 2 — Kärnsökning ✅ KOMPLETT
Hjärtat i motorn — avgör hur djupt vi kan söka.

**Komponenter:**
- Alpha-beta pruning (fail-soft negamax)
- Iterativ fördjupning med triangulär PV-tabell
- Transpositionstabell (använder Zobrist från fas 1)
- **Quiescence search** — fundamentalt, inte en optimering. Utan den blir alpha-beta taktiskt blind (horisonteffekten) och spelar fruktansvärt.
- **Dragordning:** TT-draget först → MVV-LVA → killer moves → history heuristic
- **Remi-, repetitions- och 50-dragsdetektion** i sökningen
- **Enkel tidshantering** — mjuk/hård gräns, ren abort som returnerar senast avslutade iterations bästa drag
- **`bench`-kommando** — deterministisk nodsignatur
- **SPRT-testramverk** (fastchess) — börjar redan här, inte i fas 8

**Status: KLAR OCH AVSIGNERAD.** Samtliga elva byggsteg (score-konventioner, negamax, MVV-LVA, quiescence, killers/history, transpositionstabell, iterativ fördjupning + UCI info, remi-detektion, tidshantering, bench, SPRT-harness) implementerade, granskade och committade. TT-invarianten (Hash-oberoende + determinism + mate-round-trip + tripwire) håller. Ett avslutat self-play-SPRT (qsearch on/off) nådde beslut: LLR 2.96, Elo +368 ± 84, 168 partier, noll time-losses. **Elo-baslinje uppmätt via fastchess mot Stockfish (UCI_LimitStrength): ~2000 vid 10+0.1, ~2050 vid 30+0.3** — Roj är relativt starkare vid längre tidskontroll eftersom dess styrka är djup taktisk sökning utan positionell kunskap. Detta är referensbaslinjen som Fas 3–5 mäts mot. Fullständigt dokumenterad i `docs/phase2.md`.

## Fas 3 — Sökningsoptimering 🔄 UNDER PLANERING
Dessa tekniker kan dubbla eller tredubbla effektivt sökningsdjup.

**Komponenter:**
- Late Move Reductions (LMR)
- Null move pruning
- Futility pruning
- Principal Variation Search (PVS) — återinför TT-cutoffs på spelvägen som Fas 2 medvetet avstod från i PV-läge
- Aspiration windows
- **SEE (Static Exchange Evaluation)** för dragordning och beskärningsbeslut
- **Late Move Pruning / move-count pruning**
- **Razoring**
- **Singular extensions** — avancerat; förutsätter en mogen TT och sökstruktur. Byggs **sent** i fasen, villkorat av kvarvarande SPRT-budget.

**Viktig skillnad mot Fas 2:** Fas 2 hade rena "identisk-mot-orakel"-grindar (alpha-beta == minimax; Hash-oberoende). Fas 3:s tekniker ändrar *avsiktligt* sökträdet, så de kan inte verifieras mot ett sådant orakel. Den primära grinden blir istället **SPRT**: varje teknik måste bevisa ett statistiskt säkerställt Elo-tillskott mot föregående version innan den signeras av. Det är därför SPRT-harnessen byggdes redan i Fas 2.

**Status:** Kartläggs för närvarande i ett låst `docs/phase3.md`-kontrakt, enligt samma disciplin som Fas 1 och 2 — plan låses innan kod skrivs.

## Fas 4 — Klassisk evaluering
Ger schackkännedom utan neuralt nätverk.

> **⚠️ Strategiskt beslut som måste tas innan fasen byggs:** är den klassiska evalueringen
> **(a)** en *slängbar bootstrap* — finns bara för att få en spelande motor och generera träningsdata åt NNUE — eller
> **(b)** en *permanent komponent* som lever vidare som "stillager" i fas 6?
>
> Är svaret (a): material + piece-square tables räcker för att bootstrappa; bygg inte den sofistikerade versionen. Är svaret (b): bygg den ordentligt. Beslutet avgör investeringsnivå och om tuning är värt mödan.

**Komponenter:** material, piece-square tables, kungssäkerhet, bondestruktur, mobilitet, passerade bönder, öppna linjer.

- **Tuning (Texel-tuning / SPSA):** handsatta värden är svaga. Relevant främst om eval är permanent (beslut b) eller om bootstrap-kvaliteten är viktig.

## Fas 5 — NNUE (Neural Network Evaluation)
Det är detta som separerar världsklassengines från alla andra — obligatoriskt för nivån. Planen får inte underskatta komplexiteten: NNUE är egentligen **tre distinkta delsystem**.

1. **Träningsdata-pipeline** — miljoner till miljarder positioner med utvärderingar, genererade via egen sökning/self-play.
2. **Tränare** — vanligtvis i PyTorch, separat från C++-koden.
3. **Inkrementell C++-inferens** — accumulatorn uppdateras vid varje make/unmake.

**Två saker att planera för (inte upptäcka):**
- **Compute.** Att generera data och träna nät till konkurrenskraftig kvalitet kräver mycket GPU-tid. Detta är en flaskhals som måste planeras in i förväg, inte hanteras ad hoc.
- **Portabilitet vs hastighet.** Snabb NNUE-inferens använder CPU-specifik SIMD (AVX2/AVX-512). Detta krockar *inte* med OS-portabilitet (det handlar om CPU, inte Windows/Linux), men kräver **runtime-detektion av CPU-funktioner** och flera kodvägar för att vara både portabel och snabb.

*Originalitet:* att återimplementera en publicerad arkitektur (t.ex. HalfKA-feature-set) från grunden är originalarbete — se klargörandet under Tekniska krav.

## Fas 6 — Stilsystem (vår unika identitet)
Detta är vad som gör motorn igenkännbar och unik. Stilsystemet byggs **ovanpå** NNUE, inte istället för det.

> **⚠️ Central konceptuell spänning som måste lösas innan arkitekturen låses:** styrkan i en topengine kommer från att evalueringen är så *objektivt korrekt* som möjligt — NNUE tränas just för att förutsäga det objektivt bästa. När termer omviktas för "stil" görs motorn medvetet sämre än sin topp (det är okej, men en stil är per definition svagare än maxläget). Dessutom: när NNUE ersatt de klassiska termerna finns ingen "kungssäkerhets-ratt" kvar att skruva på — ett neuralt nät kan inte vridas som en klassisk eval.

**Stil ovanpå NNUE måste göras på något av dessa sätt — välj medvetet:**
1. Träna **flera nät** på stilfiltrerad data.
2. **Blanda** NNUE-output med klassiska stiltermer → då får fas 4:s klassiska eval sitt *permanenta* syfte (kopplar till beslut **b** ovan).
3. Ändra **sökbeteende** (contempt, risktagande, beskärningsaggressivitet) istället för eval.
4. **Konditionera nätet på en stilvektor**.

## Fas 7 — Avancerad infrastruktur
**Komponenter:** öppningsbok, Syzygy endgame-tabeller, tidshantering (fullständig version), pondering (tänka under motståndarens tid), asynkron input-listener-tråd (single-thread-låset från Fas 2 släpps här eller kring Lazy SMP).

- **Tidshantering:** den *enkla* versionen byggdes redan i fas 2. Denna fas är den sofistikerade versionen.
- **Syzygy:** själva *filerna* är standard och inte härledda, men *probningskoden* (motsvarande Fathom) måste återimplementeras från grunden för att originalitetsregeln ska hålla. Görbart men icke-trivialt.

## Fas 8 — Tävling och kontinuerlig optimering
Riktning: Lichess → CCRL ratinglistor → TCEC Division 4 → **Premier Division**. Varje steg informerar nästa omgång av förbättringar.

> **Notis:** test-/SPRT-infrastrukturen hör **inte** hemma här — den kom på plats redan i fas 2 (se infrastrukturspåret nedan). Denna fas handlar om att *köra* tävlingsstegen med en redan mogen testprocess.

---

# Parallellt infrastrukturspår
**Löper samtidigt med fas 1–2 och vidare — inte som en slutfas.** Detta var den enskilt viktigaste strukturella ändringen mot den ursprungliga planen.

- **Testramverk med SPRT** — *(startade i fas 2, aktivt genom resten av projektet)*. Varje ändring i en stark motor verifieras genom att spela **tusentals** partier mot föregående version och avgöra med SPRT (Sequential Probability Ratio Test) om ändringen är en statistiskt säkerställd förbättring. Elo-vinster är ofta **1–5 Elo** och kan omöjligt mätas med intuition eller några få partier. Verktyg: **fastchess** (valt och uppsatt); överväg **OpenBench** när Fas 5:s NNUE-datagenerering kräver distribuerad skala.
- **Perft-svit** — *(från fas 1, bevaras genom hela projektet som en icke-förhandlingsbar regressionsgrind)*.
- **Git från dag ett** — versionshanteringsdisciplin.
- **`bench`-kommando** — fast sökning som rapporterar nodantal, för att upptäcka om funktionella ändringar oavsiktligt ändrar sökningen. Byggt i fas 2; djupet höjs och om-baseline:as när Fas 3:s PVS gör djupare bench billig.
- **CI-grind (planerad/pågående):** `ubuntu-latest`-workflow som kör ASan+UBSan + bench vid varje push, för att göra Linux/sanitizer-kontrollen automatisk och permanent.

---

# Strategiska beslut att ta medvetet
Två beslut styr investeringsnivå och nedströms arkitektur och bör tas innan respektive fas låses:

1. **Evalueringsstrategi (fas 4):** klassisk eval som *slängbar bootstrap* eller som *permanent stillager*? Avgör om Texel/SPSA-tuning är värt mödan.
2. **Stil-på-NNUE-metod (fas 6):** vilken av de fyra metoderna (flera nät / blandning / sökbeteende / stilvektor)? Hänger ihop med beslut 1.

---

# Regler för Claude

### Kod
- Skriv all kod på engelska (variabelnamn, kommentarer, allt).
- Skriv alltid plattformsoberoende C++ — aldrig Windows-specifikt.
- Ingen kod får kopieras eller härledas från andra engines — allt 100 % originellt. (Att *implementera en känd teknik* från grunden är tillåtet; att *kopiera kod* är det inte.)
- Kompilera alltid med: `g++ -O3 -std=c++17` (projektstruktur: `src/` och `tests/`), rent under `-Wall -Wextra -Wpedantic`.

### Förklaringar
- Förklara alltid på svenska.
- Förklara vad ett *helt kodblock* gör — inte rad för rad.
- Förklara *varför* vi väljer en lösning på konceptnivå, inte för varje enskild rad.

### Vägledning — HÖGSTA PRIORITET
- Påminn aktivt om vi avviker från planen.
- Om ett fel pågår som påverkar projektet stort — stoppa och korrigera direkt, även om det betyder att kasta bort redan gjort arbete.
- Var alltid ärlig även om det inte känns bra. Ge aldrig falska förhoppningar.
- Bedöm extern feedback punkt för punkt med full teknisk ärlighet; adoptera giltiga punkter och inkorporera dem i Definition of Done.
- Håll alltid det långsiktiga målet i sikte — kompromissa aldrig med kvalitet för snabb progress.
- Planen är ett levande dokument: uppdatera den när luckor identifieras, innan implementation fortsätter.
- Slutprodukten och visionen kommer alltid före allt annat.
- Claude Code sköter all git; människan kör aldrig git manuellt.

### Kommunikation
- Svenska i all kommunikation utom koden.
- Inga onödiga förklaringar av varje kodrad — fokusera på helheten.

---

# Utvecklingsmiljö
- **OS:** Windows (men all kod plattformsoberoende; Linux-kompatibilitet inbyggt)
- **Byggverktyg (primärt):** MSYS2 **UCRT64**-skalet med `g++ 15.2.0` — detta är det enda och primära byggverktyget genom hela projektet, inte en reservlösning. Kanoniskt bygge: `g++ -O3 -std=c++17 src/*.cpp -o Roj`.
- **Editor:** Visual Studio Code, som värd för Claude Code (implementationsassistenten).
- **Sanitizers:** ASan + UBSan (Linux, inkl. WSL2/CI); UBSan trap-läge på Windows (MinGW saknar `libasan`).
- **Testramverk:** fastchess (SPRT + Elo-gauntlets), Stockfish (referensmotor för Elo-kalibrering via `UCI_LimitStrength`), Cute Chess (GUI-verifiering och egna gauntlets).

---

# Nuvarande status

**Fas 1: KOMPLETT.** Perft-porten grön på alla sex standardställningar, hash-invariant tripwire-bekräftad, ASan+UBSan rena på Linux, egna magic-tal. Se `docs/phase1.md` (låst).

**Fas 2: KOMPLETT OCH AVSIGNERAD.** Alla elva byggsteg klara: score-konventioner, fail-soft negamax, MVV-LVA, quiescence search, killers/history, transpositionstabell (Hash-oberoende TT-invariant), iterativ fördjupning med triangulär PV och full UCI `info`, remi-detektion, enkel tidshantering, `bench`, och SPRT-harness med ett avslutat self-play-SPRT till beslut. Se `docs/phase2.md` (låst).

**Elo-baslinje:** Roj mäter ungefär **2000 Elo vid 10+0.1** och **2050 Elo vid 30+0.3** mot Stockfish (`UCI_LimitStrength`), uppmätt via fastchess. Detta är rent sökstyrka — evalueringen är fortfarande bara material + en liten piece-square table. Detta är referensbaslinjen som varje kommande fas mäts mot.

**Fas 3: under planering.** Ett låst `docs/phase3.md`-kontrakt tas fram enligt samma disciplin som Fas 1–2 (plan låses innan kod skrivs). Fas 3 skiljer sig från Fas 1–2 genom att dess tekniker (LMR, null-move, PVS m.fl.) avsiktligt ändrar sökträdet och därför inte kan verifieras mot ett rent orakel — den primära grinden blir SPRT mot föregående version för varje teknik.

**Repo-tillstånd:** en gren (`main`), rent arbetsträd, all historik committad. `docs/phase1.md` och `docs/phase2.md` låsta i `docs/`. `sprt/` innehåller det committade fastchess-harnesset (skript, README, `.gitignore` som utesluter binärer/bok/output).
