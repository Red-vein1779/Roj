# Roj — Fas 1: Grunden

**Status: LÅST.** Detta är den slutgiltiga kartan för Fas 1. Den ändras bara
om en konkret lucka identifieras — och i så fall uppdateras dokumentet *innan*
implementationen fortsätter. Kartan är kompassen: varje steg har ett
"klart när"-kriterium, och hela fasen är underordnad en enda port — **perft**.

---

## 1. Vad Fas 1 är — och inte är

Fas 1 bygger inte en engine som spelar bra. Den bygger en engine som kan
schackreglerna *perfekt*: vet exakt vilka drag som är lagliga i varje ställning,
och kan göra och ångra drag utan att tappa en bit information.

Draggeneration är den mest buggdrabbade delen av varje schackprogram. En subtil
bugg kraschar ingenting — den producerar bara ett olagligt drag en gång på
tiotusen ställningar. När sökning, evaluering och NNUE ligger ovanpå är en sådan
bugg närmast omöjlig att hitta; symptomet blir "engine:n spelar konstigt ibland",
inte ett tydligt fel. Därför är hela fasen byggd kring perft, och vi lämnar inte
Fas 1 förrän perft är perfekt.

**Ärlig förväntan vid fasens slut:** engine:n kan kopplas till ett GUI och
Lichess, förstå UCI och spela *lagliga* drag — men inte *bra* drag. `go`
returnerar bara ett lagligt platshållardrag; riktig sökning är Fas 2. Bli inte
besviken när engine:n förlorar mot allt — den kan reglerna, inte att tänka.

---

## 2. Låsta arkitekturbeslut

1. **Magic bitboards framför PEXT.** PEXT (`_pext_u64`) är en x86-instruktion,
   långsam på äldre AMD och oförenlig med plattformsoberoende. Magic bitboards
   är ren, portabel C++ som fungerar identiskt överallt.
2. **Zobrist-hashning byggs in redan i Fas 1.** Inte transpositionstabellen
   (den är Fas 2), men den inkrementella hashen som make/unmake underhåller.
   Skälet: att klistra in inkrementell hashning i en färdig make/unmake senare
   är dyrt och buggbenäget.
3. **Pseudo-lagliga drag + legalitetsfilter.** Generera drag, gör draget, fråga
   "står min kung i schack?", ångra om ja. Fullt laglig generering är slutmålet
   men skjuts upp — i Fas 1 är korrekthet allt, och filtret är dramatiskt lättare
   att få perft-perfekt. Gränssnittet byggs så att en fullt laglig generator kan
   bytas in senare och valideras mot exakt samma perft-svit.
4. **Egna magic-tal.** Vi söker fram magic-talen med vår egen kollisionssökning
   vid start, i stället för att hårdkoda en publicerad tabell. Kostar några
   millisekunder vid uppstart; tar bort allt tvivel kring originalitet. Kräver
   att vi skriver vår egen ray-tracing-referens — som vi vill ha för testning ändå.
5. **From-scratch Zobrist-invariant i varje perft-nod.** Vid sidan av
   make/unmake-symmetri räknar vi hashen *från grunden* (XOR av alla pjäsnycklar
   + sida att dra + rockadrättigheter + en passant) och jämför mot den
   inkrementella i varje nod. De två talen räknas av helt olika kodvägar och
   måste bli oense så fort ett fel finns — detta fångar de **symmetriska** fel
   som ett rent symmetritest missar (t.ex. ett rockadbidrag som glöms konsekvent
   i både make och unmake).
6. **Sanitizer-bygge vid sidan av -O3.** En debug-konfiguration med
   `-fsanitize=address,undefined` körs på perft till djup 4–5. Fångar odefinierat
   skiftbeteende (`1ULL << 64`, trivialt att råka ut för i ray-loopar) och index
   utanför attacktabellerna. Sanitizers **kompletterar** perft, de ersätter det
   inte: ett rent logiskt draggenereringsfel (fel men giltigt fält, eller fel men
   inom-tabellen-index) är varken UB eller minnesfel — bara perft fångar det.

---

## 3. Originalitet — arbetsregel genom hela projektet

> **Vår kod och vår data är vår; etablerade *tekniker* är fria.**

Vi kan inte undvika alfa-beta, bitboards eller magic bitboards för att andra
uppfann dem — men de specifika talen, raderna och strukturerna ska vara våra.
Detta gäller magic-tal, Zobrist-nycklar, och senare Syzygy-probning. Inga
publicerade tabeller lyfts in; allt genereras eller skrivs av oss.

---

## 4. Komponenter i Fas 1

- **Brädrepresentation och primitiver** — `uint64_t`-bitboards (12 st: färg ×
  pjästyp) + sammansatta ockupationsbräden. Fältnumrering a1=0 … h8=63. Liten
  verktygslåda av bitoperationer (sätt/rensa/läs, popcount, LSB-plock).
- **Brädets tillstånd** — pjäser + sida att dra, rockadrättigheter, en
  passant-fält, halvdragsräknare, fullt dragnummer, löpande Zobrist-hash.
- **Förberäknade attacktabeller** — bonde/springare/kung (beror bara på fält).
  Glidande pjäser via magic bitboards.
- **FEN-tolkning och -generering** — läsa in och skriva ut godtyckliga ställningar.
- **Dragrepresentation** — 16-bitarskodning: frånfält (6) + tillfält (6) +
  flaggor (4) för promotion/slag/rockad/en passant/dubbelsteg.
- **isAttacked + schackdetektering** — "attackeras fält X av färg Y?", grunden
  bakom schack, rockadlaglighet och legalitetsfiltret.
- **Draggeneration** — alla normala drag + de tre specialfallen (rockad, en
  passant, promotion). Separat gränssnitt för enbart slagdrag (för quiescence
  i Fas 2/3 — grunden läggs nu, kostar nästan inget).
- **Make/unmake** — applicera/ångra drag med full historikstack (slagen pjäs,
  tidigare rättigheter/EP/hash) och inkrementell hashuppdatering. Bit-för-bit
  symmetriskt.
- **Perft och divide** — räknar lövnoder per djup; `divide` bryter ner per
  rotdrag för att isolera exakt vilket drag som genererar fel antal. Vårt enda
  diagnosverktyg och vår enda sanningskälla i Fas 1.
- **Grundläggande UCI** — `uci`, `isready`, `ucinewgame`, `position`, `go`
  (platshållare), `stop`, `quit`.

---

## 5. Byggordning — 16 steg

Stegen följer beroendeordningen; varje vilar på de föregående.

| # | Steg | Klart när |
|---|------|-----------|
| 1 | Typer och konstanter | Projektet kompilerar med `-O3 -std=c++17`; grundtyperna finns och compile-time-kontrollerna passerar. |
| 2 | Bitboard-primitiver | Skapa/manipulera/skriva ut ett bitboard korrekt (inkl. 8×8-utskrift för felsökning). |
| 3 | Förberäknade attacker (springare/kung/bonde) | Uppslagstabellerna fyllda; stickprov stämmer med handräkning. |
| 4 | Magic bitboards (löpare/torn/dam) | Glidande attacker stämmer mot handräknade ställningar i alla riktningar och med blockerare. Magic-talen framsökta av egen kod. |
| 5 | Brädrepresentation + tillstånd + Zobrist-init | Tom bräda kan skapas; tillståndet representeras korrekt; Zobrist-nycklar initierade. |
| 6 | FEN-tolkning och -utskrift | Startställning + ett dussin testställningar round-trippar (FEN in → FEN ut → identisk sträng). |
| 7 | Dragkodning | Alla dragtyper kodas/avkodas utan informationsförlust. |
| 8 | isAttacked + schack/pinn-info | Identifierar attackerade fält och kungsschack korrekt i testställningar. |
| 9 | Make/unmake + historik + hashuppdatering | make→unmake återställer bit-för-bit inkl. hash, över tusentals slumpdrag. |
| 10 | Draggeneration — normala drag | Alla icke-specialdrag genereras korrekt i testställningar. |
| 11 | Draggeneration — specialfall | Rockad, en passant, promotion genereras korrekt isolerat. |
| 12 | Legalitetsfilter | Endast lagliga drag återstår (gör draget → "kung i schack?" → ångra). |
| 13 | Perft och divide | Verktygen körs; `divide` kan peka ut avvikande rotdrag. |
| 14 | **PERFT-PORTEN** | Alla sex standardställningar matchar publicerade värden exakt (se §6). |
| 15 | Grundläggande UCI-loop | Svarar korrekt på handskakningen; tar emot en ställning och spelar ett lagligt drag. |
| 16 | Koppling till GUI | Ett komplett parti spelas i Cute Chess (eller liknande) utan ett enda olagligt drag. |

Steg 1–9 = infrastruktur. Steg 10–14 = hjärtat. Steg 15–16 = skalet.
Vid problem under vägen är `divide` nästan alltid svaret.

---

## 6. Perft-porten — definitionen av "klar"

Perft fungerar för att exakta lövnodsantal är publicerade och oberoende
verifierade för standardställningar. Matchar vår generator dem är den med
överväldigande sannolikhet korrekt — positionerna är utvalda för att utlösa
varje knepigt regelfall.

**Startställningen** (verifierad):

| Djup | Lövnoder |
|------|----------|
| 1 | 20 |
| 2 | 400 |
| 3 | 8 902 |
| 4 | 197 281 |
| 5 | 4 865 609 |
| 6 | 119 060 324 |
| 7 | 3 195 901 860 |

**Kiwipete** — `r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1`
(verifierad; testar samtidigt rockad åt båda håll, pinnade pjäser, slag, promotioner):

| Djup | Lövnoder |
|------|----------|
| 1 | 48 |
| 2 | 2 039 |
| 3 | 97 862 |
| 4 | 4 085 603 |
| 5 | 193 690 690 |

Standardsviten består av **sex** positioner. De fyra övriga (bl.a. ett rent
bonde-/tornslutspel och en promotionstung ställning) **hämtas live från Chess
Programming Wiki när testharnessen byggs (steg 13)** och hårdkodas därifrån —
aldrig ur minnet, inte heller modellens. En enda felaktig siffra ska larma.

**Vad perft fångar automatiskt** (de buggar alla hobbyengines fastnar på):

- En passant som avslöjar schack längs raden (slaget tar bort *två* bönder
  samtidigt och kan blottlägga kungen).
- Rockad genom, ur eller in i schack.
- Rockadrättigheter som ska försvinna när ett torn *slås* på sitt ursprungsfält.
- Promotion i kombination med slag.
- En pinnad pjäs som felaktigt tillåts flytta.
- Dubbelschack (bara kungen får flytta).
- Kungen som flyr "bakåt" längs en glidande schacks stråle.

---

## 7. Filtret och rockad — exakt

Med pseudo-lagligt + "gör draget, är min kung attackerad?, ångra" försvinner
nästan hela buggsamlingen i §6 av sig själv, eftersom vi testar kungens säkerhet
i den *faktiskt uppdaterade* ställningen.

- **Kungen flyr bakåt längs strålen** hanteras gratis: vi *gör* kungsdraget först,
  kungens gamla fält blir tomt, tornets stråle når kungens nya fält, och frågan
  "attackeras kungen?" svarar ja → draget förkastas. Buggen finns bara i
  generatorer som testar flyktfältet i den *gamla* ställningen utan att plocka
  bort kungen ur ockupationen. Vi gör inte det misstaget.

**Det enda filtret inte fångar är rockad** (det ser bara slutställningen, inte
transitfältet eller att kungen redan stod i schack). Exakt vad som behövs:

- Kungens **slutfält** (g1/c1 m.fl.) kontrolleras *automatiskt*, eftersom rockaden
  utförs som ett kungsdrag.
- Två extra villkor krävs: **(a)** kungen får inte stå i schack *nu*, och
  **(b)** *transitfältet* (f1 kungssida, d1 damsida) får inte vara attackerat.
- **Fälla:** vid lång rockad måste b1 vara *tomt*, men behöver **inte** vara
  oattackerat — kungen passerar aldrig b1, bara tornet, och torn får passera
  attackerade fält.

**Konsekvens:** Fas 1 ska ha *mindre* logik, inte mer. Inget pinn-maskineri —
bara `isAttacked` (som vi bygger ändå), filtret, och de två rockadvillkoren.
Pinn- och schack-medvetenhet kommer tillbaka i senare faser, men för **dragordning
och beskärning**, inte för laglighet.

---

## 8. Definition of Done — Fas 1

Vi är klara — och får först då röra Fas 2 — när **samtliga** är sanna:

- [ ] Perft matchar publicerade värden exakt på alla sex standardställningar,
      minst till djup 5 (startställningen och Kiwipete gärna djupare).
- [ ] **I varje perft-nod:** hash räknad från grunden == inkrementell hash,
      över hela sviten.
- [ ] make→unmake återställer ställningen bit-för-bit, inkl. Zobrist-hash,
      verifierat över hundratusentals slumpdrag.
- [ ] **Ren körning under `-fsanitize=address,undefined` till djup 4–5.**
- [ ] **Magic-talen framsökta av vår egen kod, med vår egen ray-tracing som facit.**
- [ ] Kompilerar rent med `g++ -O3 -std=c++17 src/*.cpp -o Roj` utan varningar.
- [ ] Klarar UCI-handskakningen och spelar, kopplad till ett GUI, ett helt parti
      från start till matt/remi utan ett enda olagligt drag.
- [ ] Ingen rad kod kopierad eller härledd från någon annan engine.

---

## 9. Bygg- och felsökningskommandon

**Kanoniskt bygge (release):**

```
g++ -O3 -std=c++17 src/*.cpp -o Roj
```

Fungerar identiskt på Windows (via MSYS2-skalet, där `src/*.cpp` expanderas
korrekt) och Linux. Windows → `Roj.exe`, Linux → `Roj`. Inget Windows-specifikt
införs någonsin.

**Debug-/sanitizerbygge:**

```
g++ -O1 -g -std=c++17 -fsanitize=address,undefined -fno-omit-frame-pointer src/*.cpp -o Roj_debug
```

Sanitizers är mest pålitliga på **Linux** — vilket passar, eftersom
Linux-kompatibilitet ändå ska finnas från dag ett. UBSan fångar skift-klassen
(`1ULL << 64`) även på Windows via MSYS2; full **ASan** är ett Linux-steg
(MinGW-stödet är begränsat).

**En anteckning om EP-konvention** (att spika när from-scratch-hashen skrivs i
steg 9/13): den inkrementella och den från grunden måste använda *exakt samma*
en passant-konvention, annars larmar invarianten falskt. För korrekt
remi-detektering senare bör EP räknas som särskiljande endast när slaget faktiskt
är möjligt — ett Fas 2-förfinande, men konventionen måste vara bestämd redan när
kontrollen skrivs.
