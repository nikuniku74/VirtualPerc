# Seguire una band dal vivo: cosa manca, e cosa serve da te

Scritto il 10/09/2026, su richiesta dell'utente, dopo la sessione che ha chiuso
il cancello «non lo so ancora» (item 29), l'arbitro pettine/rete e le derive
(item 31).

Questo documento ha due metà. La prima dice **cosa manca** per arrivare a
seguire bene un brano suonato dal vivo, in ordine di quanto pesa. La seconda
dice **cosa mi serve da te**, in concreto, per poterlo misurare — perché metà
delle domande aperte oggi non ha risposta non per mancanza di idee ma per
mancanza di un metro.

---

## Parte 1 — Cosa manca

### Dove siamo, in tre numeri

- Sul brano di riferimento (`01 BLUE SKY.mp3`, intro di trenta secondi senza
  batteria): primo aggancio tenuto **53.5 s**, dentro il ±2% per il **73.6%**
  del brano, e durante l'intro la parte non suona più.
- Sul banco dei dodici materiali (`probe_matrix`, 360 corse): aggancio medio
  **5.25 s**, ma **8.0 s** su un mix che si mangia i colpi e **8.2 s** su una
  band sfilacciata, con un caso peggiore da 49 s.
- Sulle derive a tempo costante: 4 uscite su 8 rientrano dentro una battuta, la
  peggiore è 4.7 s contro una battuta di 4.0 s.

Il sintomo che senti tu — *«segue ma a volte esce fuori e fa fatica a rientrare
subito»* — è il terzo di questi, misurato su materiale sintetico. Con le tue
registrazioni diventa misurabile sul materiale vero.

### 0. Cosa vuol dire «seguire», e perché oggi non è quello che fa

Richiesta dell'utente, 10/09/2026, ed è la formulazione più precisa che ci sia
in questi documenti:

> *«Anche su uno stesso brano dove il batterista che non usa clic tiene più o
> meno lo stesso bpm, capita che le percussioni non siano precise sui colpi di
> batteria. In effetti chi non è preciso è il batterista, ma come qualsiasi
> batterista non professionista che suona senza clic. L'app dovrebbe seguire
> perfettamente come un percussionista vero, e quando c'è qualche inflessione di
> tempo anche leggera dovrebbe rientrare subito nel tempo.»*

**L'app segue il tempo. Un percussionista segue il batterista.** Non è la stessa
cosa, ed è una differenza di architettura, non un difetto.

Oggi la catena stima un **BPM** con filtri lunghi e ci fa girare sopra un
orologio. Un'inflessione di una battuta non la vede, e i numeri dicono perché.
Prendiamo 120 BPM e un batterista che tira una battuta del 2%:

| | |
|---|---|
| la battuta finisce in anticipo di | **40 ms**, cioè 0.08 di battito |
| la rete riporta i battiti su una griglia di frame da | **20 ms** — ±10 ms di quantizzazione prima di ogni altro errore |
| il fit corto è lungo | **8 battiti = due battute**, quindi una battuta tirata viene diluita a metà |
| le soglie che dovrebbe superare | `kFixedMaxStep` 1.5%, `kLeaveFixedError` 2%, `kCombPullThreshold` 3% |

Un 2% su una battuta sola non supera niente. L'app ha ragione a non seguirlo
*come cambio di tempo* — non lo è — ma il percussionista quel colpo lo mette
comunque dove il batterista l'ha messo, e l'app no.

**Il meccanismo giusto esiste, ma non è disponibile in questo setup.**
`Tracking/KickOnsetDetector.h` più `BeatTracker::notifyKickOnset` fanno
esattamente quello che fa un percussionista: datano il colpo sull'audio invece
che su una griglia da 20 ms, e due colpi consecutivi danno l'intervallo, che va
dritto all'orologio come correzione di velocità (`observeOnsetPhase`, ramo
`tempoTrim`) senza passare per la rete e senza aspettare un fit da otto battiti.
Misurato: errore di fase da **17 a 13 ms rms**.

Vuole però **un canale che contenga solo la cassa**, e l'utente ha dichiarato
(10/09/2026) che non ha registrazioni con la sola cassa e che dal vivo non può
mandare all'app tracce separate. Quindi questa strada è chiusa, e vale la pena
sapere *quanto* è chiusa.

**Provato: lo stesso rilevatore sul mix.** `scripts/` scratch, il detector
invariato, alimentato con il mix invece che con un canale dedicato:

| materiale | onset trovati | attesi | griglia |
|---|---|---|---|
| promo dance, 276 s | **129** | ~550 | 56-80 s: 15/15 a 112.61 BPM, **residuo 13.3 ms** |
| | | | 168-184 s: **5 onset**, 228-250 s: **3** |
| BLUE SKY, 237 s | **77** | ~340 | residui 160-186 ms, un passaggio senza griglia |

Il difetto **non è la precisione, è la copertura**: dove il colpo lo trova, lo
data bene — 13.3 ms di residuo su una griglia rigida è un buon numero. Ma su un
mix lo trova circa un battito su quattro, e su interi passaggi non lo trova
affatto. La ragione sta scritta nel file stesso: il detector è costruito su un
canale «silenzioso esattamente per tutto il tempo in cui il batterista non
suona», e un mix non è mai silenzioso. Un mix masterizzato ancora meno.

**Non riproporre questa strada senza cambiare il rilevatore**: la misura sopra è
il motivo.

**Cosa resta, in ordine onesto.**

- [ ] **Raffinare sotto il frame il tempo del battito neurale.** La griglia da
  20 ms porta ±10 ms di quantizzazione prima di ogni altro errore; interpolare
  il picco dell'attivazione fra due frame la dimezza. Aiuta la precisione sui
  colpi, **non** l'inseguimento di un'inflessione.
- [ ] **Il modello.** Attivazioni più nette e meglio localizzate su un mix dal
  vivo migliorano insieme precisione e prontezza. Tutto converge lì.
- [ ] **Distinguere i due sintomi**, che sono problemi diversi: se la parte è
  **sempre** in ritardo (o sempre in anticipo) della stessa quantità è
  *taratura*; se **vaga**, è *inseguimento*. A orecchio si separano in un
  ascolto, e chiudono metà del campo.

**E il limite onesto, che va detto.** Un percussionista vero ha due vantaggi che
l'app non può avere: **vede** il batterista, e **conosce il pezzo**. Anticipa;
l'app può solo reagire. Quello che è realisticamente raggiungibile è stare entro
una decina di millisecondi in media e rientrare da un'inflessione dentro la
battuta — non essere *davanti* alla spinta come fa un umano.

E c'è una tensione reale: un percussionista insegue l'*intenzione*, non ogni
oscillazione, e lo scarto lo lascia stare. Se l'app inseguisse tutto lo scatter
la parte suonerebbe nervosa. Quel giudizio, nel motore, è `FollowStrength` — già
su **HIGH** di default — e l'orologio **piega la velocità** invece di saltare,
così nessun colpo viene mai suonato due volte o saltato. Chiudere 0.08 di
battito con una piega del 5% costa circa un battito e mezzo: quello è il
pavimento.

### 1. Il modello è il tetto, e tutto il resto viene dopo

BeatNet è addestrato su musica con la batteria. Su un intro di voce e chitarra,
o su un pezzo con solo pad e accordi, non è incerto: è **confidente e
sbagliato**. Con un pad sopra, 52 / 100 / 168 BPM vengono letti 147 / 60 / 83.
Non è una soglia da ritoccare — è che la rete non ha mai visto quel materiale.

Finché resta così, tutto il lavoro a valle è gestione del danno. Il cancello
costruito ieri non fa entrare la parte finché non c'è una sezione ritmica, ma
non è che nel frattempo sappia il tempo: sa solo di non saperlo.

**Serve un modello beat/downbeat addestrato anche su accompagnamenti tonali.**
È il pezzo di lavoro più grosso che resta, ed è una decisione, non un fix.

### 2. Manca la verità di fase, quindi metà delle domande non ha risposta

Il bersaglio è 8 ms fra dove suona la parte e dove cade il beat. Oggi si misura
solo su materiale sintetico, dove la griglia è nota perché l'abbiamo scritta
noi. Su una registrazione vera non esiste una griglia annotata, quindi i 26-33 ms
ai livelli bassi, i 61 ms a −18 dB e i picchi isolati da 85-167 ms **non si
confrontano con niente**.

Costa poco e sblocca molto. È la Parte 2 di questo documento.

### 3. Come entra il segnale conta più di qualunque algoritmo — ed è il mestiere vero

- Un **canale dedicato alla cassa** (una mandata dal banco) data il beat al
  campione invece che a una finestra da 20 ms: errore di fase da **17 a 13 ms
  rms**. È già implementato, va solo usato.
- Un **feed diretto dal mixer** batte il microfono di parecchio.
- Il microfono in stanza è il caso peggiore, e il limite è fisico: il canceller
  toglie il **91%** di quello che l'app suona quando il ritorno è pulito, ma solo
  il **16%** con una parete e il **6%** con la coda di riflessioni completa. In
  stanza l'app sta in parte seguendo se stessa, e non è un difetto da correggere
  — è la forma del modello.

Il percorso migliore sarebbe mandata diretta dal banco più un canale cassa
dedicato: sposta più millisecondi di qualunque cosa si possa fare nel decoder e
non costa una riga di codice. L'utente ha però dichiarato (10/09/2026) che dal
vivo non può mandare tracce separate, quindi resta **una sola mandata mista**, e
tutto il resto va progettato attorno a quel vincolo. Vedi il punto 0.

### 4. L'ottava non è risolvibile dall'audio

Un groove a 50 BPM con hi-hat sugli ottavi e un half-time a 100 BPM **sono lo
stesso suono**: stesse spaziature, stessa energia bassa sotto gli stessi colpi.
Uno è giusto e l'altro è sbagliato, e nell'audio non c'è niente che li
distingua, perché quale dei due livelli si chiami «il tempo» è una convenzione,
non un fatto acustico. Tre tentativi di deciderlo dall'analisi sono documentati
e falliti (`docs/HANDOFF_OCTAVE_50BPM.md`).

La via d'uscita è fuori dall'audio: **TAP, o i pulsanti ÷2 / ×2**, sempre
raggiungibili.

### 5. Il muro che si ripresenta ogni volta

Quando il tempo cambia davvero, il pettine — la stima dagli intervalli fra i
colpi — **continua a nominare il tempo appena lasciato per qualche secondo**. E
quando il tempo *non* è cambiato ma la rete sbaglia, il pettine ha ragione
subito. I due casi sono indistinguibili dall'interno per un paio di secondi.

Ci sono morte tre correzioni plausibili in una sola giornata, una delle quali
faceva collassare un gradino 120 → 160 a 53 BPM. Per romperlo serve **una
seconda stima del tempo insieme veloce e indipendente**, che oggi non esiste: la
rete è veloce ma sbaglia, il pettine è affidabile ma lento. È di nuovo il
punto 1.

### Cosa NON fare

Misurato, e vicoli ciechi:

- **Non toccare il guadagno d'analisi** sulla base di un brano: è misurato che
  non è la causa, e le sue costanti sono tarate altrove.
- **Non provare a decidere l'ottava nel decoder** (punto 4).
- **Non usare il pettine come veto su un cambio di tempo** (punto 5).
- Non fidarsi di un banco solo: `probe_matrix` esiste perché un banco più
  stretto riportava 1.78 s di aggancio medio dove quello largo ne riporta 5.30.

### In che ordine

1. **Una registrazione dalla mandata vera, con i tuoi tap sopra** — costa due
   minuti a brano e sblocca ogni giudizio sulla fase.
2. **Distinguere taratura da inseguimento** a orecchio — costa un ascolto e
   dimezza il campo.
3. **Raffinare sotto il frame** il tempo del battito neurale — ±10 ms gratis.
4. **Il modello** — il vero tetto, ed è un progetto.

Il canale dedicato alla cassa resta la strada migliore in assoluto, e va
ricordata se un giorno il setup dal vivo cambia: è già implementata e aspetta
solo un canale.

---

## Parte 2 — Cosa mi serve da te

**Il bersaglio è il live, e solo il live.** Caricare un file nell'app è una
comodità di prova, non il mestiere: serve a isolare il tracker da tutto il
resto. Ma il segnale che l'app dovrà seguire sul palco è un'altra cosa — passa
per un microfono o per una mandata del banco, ha la stanza dentro, ha il nostro
stesso suono che rientra, e soprattutto ha un tempo che **respira**, cosa che un
file masterizzato non fa.

Da qui viene la richiesta più importante di tutto il documento, ed è al punto
zero qui sotto perché costa quasi niente a chi già registra i concerti.

### 0. Registra il prossimo concerto **dalla mandata che userai**

L'utente ha dichiarato che non può mandare all'app tracce separate dal vivo e
non ha registrazioni della sola cassa. Quindi niente stem, mai — e la richiesta
si semplifica a una cosa sola:

```
live.send.wav      la mandata (o il microfono) che darai all'app
```

Deve essere **lo stesso punto di prelievo che userai sul palco**, non un mix di
sala e non un master. È quello il segnale su cui l'app dovrà lavorare, con la
sua compressione, il suo rientro e la sua stanza; un mix masterizzato è un altro
problema, più facile, e misurarlo dice poco.

La griglia dei battiti arriva separatamente, dal livello 1 qui sotto.

Se non riesci, valgono comunque le tre cose sotto, in ordine di **valore diviso
fatica**.

### A. La cosa che costa meno e vale di più: dimmi *quando* è uscito

Mentre ascolti, annota i secondi in cui l'hai sentito sbagliare. Basta così:

```
BLUE SKY          1:24 esce, rientra verso 1:31
INFINITO          0:12 parte su un tempo sbagliato
                  2:47 rallenta il ritornello e lui non la segue
LA CANZONE X      3:05 dopo lo stop non ritrova l'uno
```

Dieci righe di queste valgono più di qualunque banco io possa scrivere, perché
puntano la misura al secondo giusto invece di farmi cercare. Non serve
precisione: «verso 1:24» va benissimo.

### B. Il file audio

- **WAV**, 44.1 o 48 kHz, 16 o 24 bit. Mono o stereo, indifferente.
- **Non MP3 se puoi evitarlo**: il decoder aggiunge un ritardo di padding
  all'inizio, di qualche decina di millisecondi, che è esattamente l'ordine di
  grandezza di quello che stiamo misurando. Se hai solo l'MP3 va bene lo stesso
  — dimmelo e lo tengo presente.
- **Il brano intero**, non un estratto: i punti interessanti sono proprio
  l'intro, lo stacco e il finale.

**Quali brani.** Meglio la varietà della quantità: cinque o sei file che coprano

- uno con **intro senza batteria** (il caso che sappiamo essere il peggiore);
- uno che **accelera o rallenta** in modo sentibile;
- uno con uno **stop / break** in mezzo;
- uno **lento** (sotto 80) e uno **veloce** (sopra 150);
- uno in cui **sai già** che l'app sbaglia.

### C. La griglia dei battiti — tre livelli, scegli quello che ti costa meno

Questa è la «verità di fase». Ci sono tre modi di darmela, dal migliore al più
economico.

**Livello 1 — tu batti, io raffino. Costo: due minuti a brano.**

Senza tracce separate questo diventa il livello migliore disponibile, ed è
comunque una verità di fase valida: è il metodo standard con cui si annotano i
dataset di questo campo.

Il punto è che le due cose che servono vengono da due parti diverse:

- **quale** transiente è il battito lo decidi tu, ed è un giudizio musicale che
  nessun rilevatore sa fare (la misura qui sopra lo dimostra: sul mix il
  rilevatore trova un colpo su quattro);
- **quando esattamente** cade quel transiente lo trova la macchina al campione,
  e lo fa bene (13.3 ms di residuo dove il colpo c'è).

Quindi: batti tu, grossolanamente, e io aggancio ogni tuo tap al transiente di
banda bassa più vicino. Il tuo tap dice *dove guardare*, il raffinamento dice
*dove esattamente*. Nessuno dei due da solo basta; insieme sono verità.

In pratica: importi il file in un DAW, crei una traccia MIDI, e mentre ascolti
batti un tasto su **ogni quarto** e un tasto diverso sull'**uno**. Esporti il
MIDI (o i marker) e mandami quello insieme al wav. Non ti preoccupare della
precisione — è proprio la parte che sistemo io.

Se il tuo DAW sa già agganciare le note MIDI ai transienti dell'audio, fallo tu
e mi risparmi il passaggio; ma non è necessario.

**Livello 2 — batti tu e basta, senza raffinamento. Costo: identico.**

In un DAW: importi il file, crei una traccia MIDI, e mentre ascolti batti un
tasto su **ogni quarto** e un tasto diverso (o lo stesso a velocity più alta)
sull'**uno**. Poi esporti il MIDI, oppure i marker.

Non preoccuparti della precisione: le battute a mano arrivano 20-30 ms tarde ma
in modo **sistematico**, e uno scarto sistematico si toglie. Quello che
sopravvive è la *forma* — dove il tempo si muove, dove l'uno cade — che è
esattamente quello che serve per le derive e per il rientro.

Se il DAW sa agganciare le note MIDI ai transienti dell'audio, fallo: diventa
quasi un livello 1.

**Livello 3 — solo il tempo, a occhio. Costo: dieci secondi.**

Se non hai voglia di fare né l'uno né l'altro, mi basta:

```
BLUE SKY     87 BPM circa, costante
INFINITO     92 BPM, ma il ritornello rallenta
```

Con questo posso già misurare il tuo sintomo attuale: quanto ci mette ad
agganciare, quanta parte del brano sta dentro il ±2%, quante volte esce e quanto
dura ogni uscita. **Non** posso misurare la fase — cioè se la parte suona
davanti o dietro al beat — che è il punto 2 della Parte 1.

### Formato della griglia, se me la dai come testo

Un numero per riga, **secondi dall'inizio del file**, con un `1` accanto ai
downbeat:

```
0.512 1
1.166
1.820
2.474
3.128 1
3.782
```

Va bene anche un CSV, o un MIDI, o i marker esportati dal DAW: quello che è più
comodo a te. L'importante è che i tempi siano riferiti **all'inizio del file
audio che mi mandi**, non alla timeline della sessione.

### Nomi dei file

Perché io possa appaiarli senza chiedere:

```
qualcosa.wav          audio
qualcosa.mid          i tuoi tap      (livello 1)
qualcosa.beats.txt    griglia già pronta, se ce l'hai
qualcosa.note.txt     dove è uscito   (punto A)
```

### Cosa ti restituisco

Per ogni file, una tabella:

- primo aggancio tenuto 3 s, e su quale tempo;
- percentuale del brano dentro il ±2%;
- ogni uscita: quando, quanto è durata, quanto era grande, e **perché** —
  quale delle stime aveva ragione in quel momento;
- con il livello 1 o 2, l'errore di fase in millisecondi, medio e peggiore, e
  dove stanno i picchi.

E poi, per la prima volta, un banco di regressione fatto di **materiale vero**
invece che sintetico. Oggi `VPTrack` sa già leggere un wav e misurare le prime
due cose; le altre aspettano la griglia.

---

## Farsi aiutare da ChatGPT: cosa può fare e cosa non deve fare

Il lavoro meccanico si delega bene. Il giudizio no, e qui c'è una trappola
precisa da evitare.

### La trappola

Se chiedi a ChatGPT «trovami i battiti di questo brano», userà un beat tracker
automatico (librosa, madmom o simili) e ti darà una lista di numeri
perfettamente plausibile. **Quella lista non è la verità: è la risposta di un
altro tracker.** Misurare il nostro tracker contro un altro tracker non dice
niente — se sbagliano insieme sembra che vada tutto bene, e se sbagliano in modo
diverso non sappiamo quale dei due ha ragione.

Per la fase, dove il bersaglio sono 8 ms, è peggio che inutile: è fuorviante.

**Regola:** ChatGPT può fare **rilevazione** (dove sta il transiente della cassa
in una traccia che contiene *solo* la cassa: è un fatto) e **conversione** (da
MIDI a testo, da un formato a un altro). Non deve fare **stima** (dove sta il
beat in un mix: è un'opinione).

### Cosa chiedergli, con il testo da incollare

**A. Raffinare i tuoi tap sui transienti del mix** — questo è il lavoro buono,
ed è quello che sostituisce la traccia della cassa che non hai.

> Ho due file: un WAV con il mix di una registrazione live, e un file di testo
> con i tempi in secondi in cui ho battuto a mano il tempo mentre lo ascoltavo.
> I miei tap sono in ritardo di qualche decina di millisecondi e non sono
> precisi. Scrivimi ed esegui uno script Python che, **per ogni mio tap**:
>
> 1. prende una finestra da −120 ms a +60 ms attorno al tap;
> 2. filtra il mix in banda 30-180 Hz (due one-pole in cascata bastano) e ne
>    calcola l'inviluppo su 3 ms;
> 3. trova dentro quella finestra il punto in cui l'inviluppo sale più
>    ripidamente, e poi **torna indietro** fino a dove l'energia supera il 20%
>    del suo picco locale: quello è l'attacco;
> 4. scrive il tempo dell'attacco al posto del mio tap, con quattro decimali.
>
> Poi dimmi: quanti tap sono stati spostati, di quanto in media e al massimo, e
> quanti non hanno trovato nessun transiente nella finestra (quelli lasciali
> dove sono e segnalameli).
>
> Usa solo `wave` e `numpy`. **Non usare librosa e nessun beat tracker**: la
> decisione su quale transiente sia il battito l'ho già presa io battendo, il
> tuo lavoro è solo trovarne l'istante esatto.

Perché funziona: il punto 3 è quello che vale i millisecondi, perché il picco
di una cassa arriva 20-30 ms dopo il suo attacco e il battito sta sull'attacco.
E il fatto che tu abbia già deciso *dove guardare* è quello che rende la cosa
onesta: sul mix, da solo, un rilevatore trova un colpo su quattro (misurato).

**B. Dai tuoi tap alla griglia** — anche questo va bene.

> Ho un file MIDI dove ho battuto a mano il tempo su un brano: una nota per
> ogni quarto, e una nota diversa (o velocity più alta) sull'uno. Convertilo in
> un file di testo con un tempo in secondi per riga, quattro decimali, e uno
> spazio seguito da `1` sulle righe che sono downbeat. Il tempo zero è
> l'inizio del brano. Dimmi anche il BPM medio e dove cambia di più del 3%.

**C. Controlli e conversioni** — sempre utili:

> Questi due WAV dovrebbero essere allineati al campione. Dimmi durata,
> frequenza di campionamento, canali e picco di ciascuno, e verifica che
> comincino nello stesso istante.

### Cosa NON accettare

- Una griglia prodotta da `librosa.beat.beat_track` o equivalenti sul mix.
- Una griglia «ricostruita» da un BPM costante: un gruppo dal vivo non ha un
  BPM costante, e la deriva è proprio la cosa che stiamo misurando.
- Una griglia quantizzata a una scansione regolare: la quantizzazione cancella
  esattamente l'informazione che serve.

Se ChatGPT ti dà una lista e non sai da dove viene, chiediglielo. Se la risposta
contiene la parola «beat tracking», buttala.

---

## Nota su come stai provando adesso

Caricare i file nell'app è il percorso più pulito e va benissimo. Due cose da
sapere mentre lo fai:

- Il file interno passa per `directFile`, quindi **niente canceller e niente
  stanza**: è il caso migliore. Quando poi proverai col microfono in sala,
  aspettati di peggio, e non sarà una regressione — è il punto 3 della Parte 1.
- «Esce e fa fatica a rientrare» è l'item 31 del TODO, misurato ieri su
  materiale sintetico: quattro uscite su otto adesso rientrano dentro la
  battuta, tre no. Le tue registrazioni sono il modo per vedere se sul materiale
  vero il quadro è lo stesso o diverso — sospetto diverso, perché una band vera
  ha stacchi e vuoti che il banco sintetico non ha.

## Materiale già provato

### `Promo Dance SOLO ING.mp3` (10/09/2026)

Otto minuti dichiarati, ma la **musica finisce a 4:35**: da lì alla fine sono
tre minuti e mezzo di silenzio digitale esatto, probabilmente un artefatto
d'export. Contiene una decina di brani dance attaccati, con un cambio di tempo
vero ogni 30-60 secondi: 107 → 114 → 120 → 127 → 119 → 131 → 133.

**Cosa dice.** Il motore lo segue bene. Un solo restart (all'avvio), confidenza
fra 0.85 e 1.00 per quasi tutta la durata, e ogni segue viene raggiunto. I due
costi visibili:

- **16 s per il primo aggancio corretto.** Pubblica 68 BPM per i primi dodici
  secondi, poi salta a 107 e ci resta.
- **~8 s per ogni cambio di brano.** Da 108 a 114.5 fra il secondo 52 e il 64;
  da 119 a 131 fra il 220 e il 232. In quegli otto secondi il tempo pubblicato
  sta *fra* i due, che è musicalmente il posto peggiore in cui stare.

**A cosa serve e a cosa no.** È un ottimo banco per i **cambi di tempo**, molto
più ricco dei gradini sintetici di `probe_tempo_step`, e va tenuto. Ma è un mix
masterizzato: niente stanza, niente rientro, e dentro ogni brano il tempo è
elettronico e non si muove di un millesimo. **Non dice niente sul mestiere
vero**, che è seguire un gruppo il cui tempo respira. Per quello serve il
punto 0 della Parte 2.
