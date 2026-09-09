# Ripresa del lavoro sul tempo

Richiesta: implementare progressivamente il piano approvato, con test mirati e
commit locali separati. **Ultima istruzione utente: proseguire SENZA committare**;
questa prevale sul piano iniziale. Non eseguire la suite completa. Nessuna modifica al
submodule JUCE, già sporco all'inizio. Nessuna promessa di perfezione su audio
ambiguo: mantenere il tempo acquisito, oppure attendere/TAP all'avvio.

## Stato

### Checkpoint credito limitato — 09/09/2026: rifinitura iniziale mantenuta

Utente con 17% credito: un solo esperimento circoscritto, nessun commit.
`BeatDecoder::tryFastAcquire`: su linea, dopo la scelta del livello, media due
intervalli già presenti se concordano entro 14%. Non aggiunge attesa, non rifiuta
l'aggancio se discordano, non modifica celle swing o ingresso microfonico.

INFINITO estratto ~30–70 s, avvio freddo, vero ONNX:
- a +2 s decoder 95.43 -> 92.27 BPM, clock 95.57 -> 92.44;
- a +4 s decoder 95.43 -> 89.76, a +6 s 90.91 -> 90.37;
- deriva relativa media/massima 17.5/81.1 -> 9.0/25.1 ms; BPM medio 91.11.
Non sono attacchi audio renderizzati, né una fase assoluta certificata. Un run
per variante, non prova di determinismo della rete. La stima non è perfetta a +2 s.

Regressione ridotta `probe_matrix --quick` (72 casi): aggancio medio 7.28 ->
7.30 s, corse con escursioni 18 invariato, sei half-time non agganciati invariati;
fuori soglia 13.33 -> 13.31%. Piccoli peggioramenti di acquisizione su alcune
forme (fino a +0.18 s nelle medie di stile), nessun aumento del numero di
escursioni. Mantenuta per il vantaggio misurato sul caso reale, NON dichiarata
soluzione generale dei ritardi. Non eseguita suite completa.

```
cmake --build build-host --target VPLive -j4
./build-host/VPLive_artefacts/Release/VPLive --mix /tmp/vp-infinito-review.DdwGeQ/center40.wav --bpm 91 --trace
c++ -std=c++17 -O2 -ISource scripts/probe_matrix.cpp Source/AI/BeatDecoder.cpp Source/AI/TempoEstimator.cpp Source/AI/BeatHmm.cpp -o /tmp/vp-acquire-average
/tmp/vp-acquire-average --quick
```

Prossimo: provare il brano nell'app tramite input diretto/mixer, poi dump
deterministico e regressione dedicata alla prima stima se serve ulteriore tuning.
Restano aperti i ritardi su altri materiali e la verifica degli attacchi effettivi.

### File nuovamente disponibile — 09/09/2026

L'utente ha riallegato `3 INFINITO.mp3`, ora leggibile nel percorso originale.
VPLive ricompilato sul codice corrente. Nessuna nuova modifica DSP o commit.
Conversioni solo nella directory temporanea `/tmp/vp-infinito-review.DdwGeQ`.

```
cmake --build build-host --target VPLive -j4
/opt/homebrew/bin/mpg123 -q -w /tmp/vp-infinito-review.DdwGeQ/infinito.wav '/Users/nicolamarogna/Desktop/3 INFINITO.mp3'
./build-host/VPLive_artefacts/Release/VPLive --mix /tmp/vp-infinito-review.DdwGeQ/infinito.wav --trace
/opt/homebrew/bin/mpg123 -q -k 1148 -n 1531 -w /tmp/vp-infinito-review.DdwGeQ/center40.wav '/Users/nicolamarogna/Desktop/3 INFINITO.mp3'
./build-host/VPLive_artefacts/Release/VPLive --mix /tmp/vp-infinito-review.DdwGeQ/center40.wav --bpm 91 --trace
```

File intero: tracker circa 72.8 a 2–6 s, 91.3 a 14 s; assestamento automatico
18 s. Non chiamare l'intro un errore di BPM senza una griglia musicale annotata.
L'estratto centrale (~30–70 s del file) è il riproduttore utile: da avvio freddo
clock/decoder 95.57/95.43 a +2 s, 95.43/95.43 a +4 s, 92.31/90.91 a +6 s,
91.45/90.84 a +8 s, 91.17/90.97 a +10 s. BPM medio dopo warm-up 91.05.
Deriva relativa al riferimento costante: media 17.5 ms, massimo 81.1 ms;
non è fase assoluta, né misura di attacchi renderizzati. Il banco bypassa il bus
di make-up/leak dell'app, quindi non certifica il mixer fisico.
Prossima correzione da verificare: prima griglia provvisoria imprecisa e sua
rifinitura tardiva su questo estratto; usare un dump delle attivazioni per A/B
deterministico prima di cambiare le soglie. Il ritardo resta INCOMPLETO.

### Indagine aggancio iniziale — 09/09/2026 (INCOMPLETA)

Richiesto riconoscimento più veloce sul brano/mixer. Il file precedente
`/Users/nicolamarogna/Desktop/3 INFINITO.mp3` non esiste più; richiesto all'utente
il brano attuale e il timestamp. Nessuna nuova correzione DSP mantenuta in questa
indagine: due esperimenti sul solo ingresso diretto sono stati rimossi.

Banco ridotto `probe_matrix --quick`: 12 materiali × 52/120/168 × 2 semi,
60 s sintetici; misura primo BPM entro 2% che resta tale per 3 s, NON ingresso
audio. Baseline: media 7.28 s, 18 corse con escursioni, 13.33% fuori soglia.
1. Consentire picchi fuori griglia durante la sola acquisizione HMM provvisoria
   senza intervalli: risultati identici, nessun miglioramento dimostrato.
2. Richiedere coerenza dei due intervalli anche sulla linea: media 7.41 s,
   20 corse con escursioni, 14.16% fuori. Accordi 7.79 -> 4.94 s, ma rock ottavi
   8.19 -> 12.40 s: regressione, esperimento rimosso integralmente.

Comando usato per ciascuna variante:
```
c++ -std=c++17 -O2 -ISource scripts/probe_matrix.cpp Source/AI/BeatDecoder.cpp Source/AI/TempoEstimator.cpp Source/AI/BeatHmm.cpp -o /tmp/vp-acquire-before
/tmp/vp-acquire-before --quick
```
Gli altri eseguibili temporanei erano `/tmp/vp-acquire-after` (esperimento 1)
e `/tmp/vp-acquire-coherent` (esperimento 2). Non sono il codice finale.
Prossimo passo: brano realmente problematico -> dump attivazioni -> distinguere
prima stima valida, prima stima corretta e ingresso audio; poi test di regressione
mirato. Non dichiarare risolto l'aggancio lento. Modifiche precedenti preservate.

1. COMPLETATO: sicurezza del rientro (annullamento, reset, prove fresche).
2. COMPLETATO: contatori indipendenti dal buffer.
3. COMPLETATO (clock sintetico): recupero dello scarto con fiducia alta.
4. COMPLETATO (integrazione con date accordi note): percorso armonico diretto.
5. INCOMPLETO: il percorso armonico su audio sintetico ritmico ora aggancia e
   rende sul tempo, ma non entro due battute; il caso con pad fallisce ancora.
   La registrazione live e' stata provata. Il difetto iniziale 212 BPM a -12 dB
   e' stato corretto nel successivo punto 2 (vedi cronologia sotto). L'ultima
   segnalazione riguarda il rientro lento: corretto un blocco della conferma
   in presenza di suddivisioni, con verifica del solo clock nel checkpoint
   qui sotto. Il riconoscimento e il rientro audio completi restano aperti.

## Ultimo checkpoint — rientro lento a circa 120 BPM, 08/09/2026

Nuova segnalazione dell'utente: la parte a volte accelera/decelera su un brano
intorno a 120 BPM e torna troppo lentamente. Chiesti nome e istante del brano;
non ancora ricevuti durante questa verifica. Non assumere che sia INFINITO,
che nelle prove precedenti aveva una parte centrale intorno a 91 BPM.

Riproduzione aggiunta a `scripts/probe_recovery.cpp`: clock già in marcia,
fase sbagliata per due secondi (da t=4 a t=6), poi ritorno della fase corretta;
osservazioni indipendenti ogni beat. Misurare l'ULTIMO ingresso nella banda di
8 ms, e richiedere che vi resti almeno due beat, non soltanto il primo passaggio
per lo zero. Le precedenti prove partivano da un offset applicato con snap e
non esponevano la memoria del filtro accumulata durante una deviazione.

Trovati e corretti in `TempoFollower`:

1. Durante la correzione rapida, il filtro ordinario conservava una vecchia
   stima: alla scadenza poteva continuare a spingere e uscire di nuovo dal tempo.
   Ora l'errore filtrato e la memoria della derivata seguono l'errore grezzo già
   confermato; la correzione realmente applicata viene sottratta una sola volta.
2. La finestra fissa di mezzo beat, con rail del 20%, poteva correggere al più
   0.1 beat, scadendo prima di chiudere scarti maggiori. Ora dura il massimo tra
   mezzo beat e distanza residua fuori da 7.5 ms divisa per 0.20. Il rail resta
   invariato, la finestra resta sotto 1.25 beat; sono ammessi scarti confermati
   sotto 0.25 beat anziché 0.15. Restano due osservazioni concordanti, fiducia,
   annullamenti, precedenza delle transizioni e continuità della griglia.

Comandi eseguiti, soltanto probe standalone:

```bash
c++ -std=c++17 -O2 -ISource scripts/probe_recovery.cpp \
  Source/Tracking/TempoFollower.cpp -o /tmp/vp-recovery
/tmp/vp-recovery
/tmp/vp-recovery --slow-passages
```

Default: **84 PASS, exit 0** (36 rientri 120/168, 18 suddivisioni, 12 precedenti
recuperi, 18 controlli negativi). Buffer 64/256/1024, entrambi i segni. A buffer
256, tempi dal ritorno della fase corretta alla permanenza entro 8 ms:

| BPM | Scarto del passaggio | Prima | Dopo |
| --- | --- | --- | --- |
| 120 | 0.075 beat | 0.747 s | 0.747 s |
| 120 | 0.125 beat | 3.605 s | 0.752 s |
| 120 | 0.200 beat | 1.253 s | 0.880 s |
| 168 | 0.075 beat | 2.581 s | 0.608 s |
| 168 | 0.125 beat | 2.533 s | 0.608 s |
| 168 | 0.200 beat | non stabile nella finestra | 0.699 s |

Intervalli fra impulsi durante il rientro 0.83–1.25 volte il nominale, entro
il gate 0.7–1.3; nessun salto o doppione rilevato. Rumore, rampa, outlier,
raffiche ravvicinate e singolo errore di fase di 160 ms non attivano il recupero
rapido; errore uguale al controllo senza conferme a 52/120/168 BPM.

**Limite riproducibile, ancora aperto:** `--slow-passages` aggiunge 18 casi a
52 BPM e termina con **84 PASS / 18 FAIL, exit 1**. I sei casi a buffer 256
fallivano già prima delle modifiche: il rientro può lasciare un residuo troppo
piccolo per confermare il recupero (soglia 0.04 beat) ma superiore agli 8 ms,
vicino alla deadband ordinaria di 0.012 beat = 13.85 ms a 52. Anche il cooldown
di un recupero precedente richiede indagine. Non abbassare alla cieca le soglie:
va misurato il compromesso con rumore e swing a tempo lento.

Questa correzione migliora il clock a 120/168; NON chiude lo step 5 né prova che
la stima del decoder sul brano reale sia già corretta. Il prossimo passo resta
misurare il tratto reale indicato dall'utente, tracciando BPM/fase/fiducia della
rete, conferma/termine del recupero e attacchi audio. Se il decoder rimane sul
BPM sbagliato, questa correzione del clock non elimina quell'attesa. Nessuna
nuova build iPad, suite completa o commit in questa iterazione.

## Checkpoint precedente — rientro e suddivisioni, 08/09/2026

L'utente segnala che, perso il tempo, a volte la parte impiega troppo a tornare.
Trovato un difetto indipendente dall'ottava: `observeRecoveryBeat` richiede
osservazioni distanti oltre 0.55 beat, ma ogni seriale nuovo sostituiva il
candidato e azzerava la sua eta'. Con picchi accettati a ottavi l'eta' restava
sempre a circa 0.5 beat: nessuna conferma rapida, soltanto il lento controllo
ordinario. La ripetizione della stessa pubblicazione era gia' protetta; erano
i seriali diversi dei colpi intermedi a causare questo blocco.

Correzione minima in `Source/Tracking/TempoFollower.cpp`: finche' il candidato
non ha superato 0.55 beat, conservare osservazione, eta' e correzione applicata
dal clock. Un colpo intermedio non conferma e non rimanda la conferma. Restano
invariati i requisiti di concordanza/fiducia, il limite di scarto <0.15 beat,
il rail del 20%, la precedenza delle transizioni di tempo e gli annullamenti.

Verifica mirata (nessuna suite completa e nessun commit):

```bash
c++ -std=c++17 -O2 -ISource scripts/probe_recovery.cpp \
  Source/Tracking/TempoFollower.cpp -o /tmp/vp-recovery
/tmp/vp-recovery
```

- Prima: 18/18 nuovi casi con suddivisioni FALLITI, mai confermata la correzione.
- Dopo: 35/35 PASS (18 suddivisioni + 12 recuperi precedenti + 5 controlli).
- Suddivisioni: 52/100/168 BPM, scarto iniziale +/-0.075 beat, buffer
  64/256/1024. A 256: conferma in 1.157/0.603/0.357 s; convergenza aggiuntiva
  in 0.576/0.299/0.176 s. Totale dalla prima osservazione affidabile:
  **1.733/0.901/0.533 s**, scarto <8 ms e tenuta oltre due beat. Il controllo
  ordinario senza recupero rapido non raggiunge 8 ms entro i cinque beat della
  finestra. Intervalli fra impulsi controllati: nessun salto/doppione.
- Controlli: rumore, outlier, rampa, raffica di seriali nuovi entro mezzo beat,
  outlier isolato fra ottavi. Nessuna attivazione, errore uguale al controllo.

Limite: e' una regressione del clock con fase corretta iniettata, non una prova
che il brano live sia riconosciuto in questi tempi. La correzione non accorcia
un errore del decoder o l'attesa di fiducia sufficiente. Lo step 5 rimane aperto.
Prossima azione: sul passaggio reale che esce dal tempo registrare nello stesso
probe BPM/fase del decoder, seriali beat, fiducia, conferma del recupero e
attacchi renderizzati; separare ritardo della rete dalla convergenza del clock.
Per attribuire gli errori assoluti serve la beat-grid annotata gia' richiesta.
Proseguire con prove mirate; mantenere le correzioni successive di Claude
documentate nella cronologia di questo file e il submodule JUCE preesistente.

## Regole per riprendere

Leggere `.claude/skills/realtime-tempo/SKILL.md`, questo file e `git status`.
Continuare dal primo step incompleto. Salvare qui comandi, risultati, limiti e
prossima azione dopo ogni step; non creare commit finché l'utente non lo richiede.
I precedenti 8 ms / mezzo beat erano misure del solo
clock con fase esatta e fiducia simulata, non dell'app sul brano live.

## Verifica prevista

Probe brevi a 52/100/168 BPM, entrambe le direzioni; jitter, ritorno di evidenza
povera, reset, pubblicazioni ripetute e buffer 64/256/1024. Separare ritardo di
riconoscimento, convergenza del clock e attacchi audio. Per l'armonia verificare
inizializzazione a sample rate diverso, scadenza, accordi radi/sostenuti,
ingresso effettivo e ritorno della batteria. Il percorso acustico armonico
resta disabilitato finché non supera una verifica con stanza e ritorno proprio.

## Step 1 — verifica

`c++ -std=c++17 -O2 -ISource scripts/probe_recovery.cpp Source/Tracking/TempoFollower.cpp -o /tmp/vp-recovery && /tmp/vp-recovery`

6/6 PASS, 52/100/168 BPM, entrambi i segni. Due seriali distinti confermano;
copie della stessa pubblicazione non contano. Peggioramento, snap, reset,
forceTempo e transizione confermata cancellano il recupero. Da conferma a 8 ms:
0.571 / 0.299 / 0.176 s; errore successivo <8 ms per oltre due beat. Questo è
il clock, non il modello reale. Il vecchio `probe_steer --reentry` viene rifiutato
con istruzioni perché non forniva beat freschi. Prossimo: step 2.

## Step 2 — verifica

`cmake --build build-host --target VPTests -j4`
`./build-host/VPTests_artefacts/Release/VPTests --state-timing`

3/3 PASS: soglia di quattro secondi attraversata a 4.001333 / 4.005333 /
4.010667 s con buffer 64/256/1024, entro un callback. Anche il filtro della
fiducia ora usa il tempo reale, preservando la risposta precedente a 256/48k.
Step 1 salvato nel commit cdcb33c. Prossimo: step 3.

## Step 3 — verifica

Stesso comando standalone dello step 1: 12 casi di recupero + 3 controlli.
Fiducia alta: conferma dopo 1.157 / 0.603 / 0.357 s, convergenza aggiuntiva
0.576 / 0.299 / 0.176 s a 52/100/168; entrambi i segni, errore stabile <8 ms.
Rumore di fase, outlier isolato e rampa: nessuna attivazione, errore uguale al
controllo senza conferme. Due beat devono concordare entro 0.015 beat dopo la
compensazione, entrambi oltre max(0.04 beat, 20 ms). Cooldown di 2.5 beat.
La verifica dello swing del decoder e dell'audio completo resta allo step 5.
Prossimo: step 4. Step 2 commit d52f099.

## Step 4 — verifica e limite importante

`cmake --build build-host --target VPTests -j4`
`./build-host/VPTests_artefacts/Release/VPTests --harmonic-entry`

4/4 PASS: a 44.1/48 kHz il tracker entra senza worker neurale, grazie a otto
cambi di accordo datati, 100.000 BPM. PercussionEngine produce sei attacchi
misurati: errore massimo 2.59/0.23 ms. Microfono escluso, ritorno del BPM
neurale prioritario, scadenza dopo due battute (max 12 s), reset completo.
Il selettore armonico sceglie ora il massimo locale anziché il fianco della
curva: il vecchio confronto dei pareggi portava 100 esatti a 98.039.

LIMITI: questa prova bypassa il rilevatore di accordi, non il tracker/render.
Con un accordo per battuta servono otto accordi: ingresso a 18.79 s, NON entro
due battute. L'armonia rada da sola non soddisfa quell'obiettivo. Nessun nuovo
modello di pulsazione non percussiva è stato aggiunto. Step 5 deve misurare la
catena con il rilevatore e distinguere le registrazioni reali dai sintetici.

## Checkpoint finale — 08/09

Commit degli step: `cdcb33c`, `d52f099`, `cdf807f`, `9fcaf02`.
Ultimo controllo aggiunge annullamento del recupero anche su cambio d'ottava e
ricostruzione della griglia neurale; il probe verifica anche il cambio d'ottava.

Comandi mirati eseguiti:

- `c++ -std=c++17 -O2 -ISource scripts/probe_recovery.cpp Source/Tracking/TempoFollower.cpp -o /tmp/vp-recovery && /tmp/vp-recovery`: 15 casi PASS.
- `cmake --build build-host --target VPTests -j4`: compilazione riuscita.
- `./build-host/VPTests_artefacts/Release/VPTests --state-timing`: 3 PASS.
- `./build-host/VPTests_artefacts/Release/VPTests --harmonic-entry`: 4 PASS.
- `./build-host/VPTests_artefacts/Release/VPTests --tempo-slow`: 10 PASS, inclusi swing e ritorno dopo gap.
- `./build-host/VPTests_artefacts/Release/VPTests --octave 100-file`: 4 PASS. BeatNet reale su KIT SINTETICO: 99.97 BPM, ottava zero, FOLLOWING per 41.2 s, fase media +1.86 -> -1.23 ms, 137 colpi, zero gap, worker sincronizzato.

Le medie di fase del test BeatNet non sono un massimo degli attacchi audio.
Gli attacchi audio sono stati misurati separatamente nel test armonico con date
note. La registrazione live dell'utente e' stata provata nel checkpoint seguente;
non e' stata fatta una nuova build su iPad. Non e' stata eseguita la suite
completa.

## Prossima azione concreta

Il nuovo `--harmonic-audio` alimenta HarmonicChange con i soli stems musicali
di `probe_song_render.h` (basso e melodia, con/senza pad sostenuto), poi usa
BeatTracker e PercussionEngine nello stesso percorso. 36 s sintetici a 100 BPM,
48 kHz, buffer 256, seed 42; nessuna data di accordo iniettata, nessun worker
neurale. Include gli eventi iniziali del detector come in produzione. Esclusi
condizionamento del bus, stanza e ritorno delle percussioni. Il caso col pad
NON è un accordo isolato senza pulsazione: conserva basso e melodia.

Comandi eseguiti (nessuna suite completa, nessun commit):

- `cmake --build build-host --target VPTests -j4`: PASS.
- `./build-host/VPTests_artefacts/Release/VPTests --harmonic-audio`: exit 1, due FAIL, circa 0.8 s host.
- `./build-host/VPTests_artefacts/Release/VPTests --harmonic-entry`: quattro PASS, controllo con date note invariato.

Seconda iterazione, senza commit: `HarmonicChange` ora usa in produzione lo
stesso warm-up di quattro secondi che il vecchio VPSing applicava solo nel
probe. Gli eventi instabili di inizializzazione non contaminano più
HarmonicTempo. La selezione della fonte non richiede più anche il lento
`tonalShare > 0.55`: una fase armonica valida è già fondata su otto cambi
freschi e coerenti; qualsiasi BPM neurale resta immediatamente prioritario.
Il gate di tonalità rimane sul percorso separato che sposta la battuta.

Risultati del gate, che richiede ingresso entro 4.8 s e attacchi entro 25 ms
nelle ultime due battute:

- Senza pad: 13 cambi; fase e fonte valide da 22.096 s, ingresso a 23.371 s,
  99.917 BPM, coerenza 0.997. Otto impulsi del clock e otto attacchi; errori
  massimi 22.27 ms e 18.17 ms. Funziona, ma fallisce il limite di 4.8 s.
- Con pad: 34 cambi; fase mai valida, coerenza finale 0.355. Nessun ingresso,
  nessun attacco. Il solo numero di eventi non prova quali siano falsi.
- Batteria sola: 7 cambi, quota tonale max 0.252, nessuna fase/BPM: astensione PASS.
- Accordo continuo: quota tonale max 0.814 ma zero cambi e zero BPM: astensione PASS.
- Errori fase/clock/audio stampati a -1 quando non misurabili: NON zero errore.

Terza iterazione, senza commit: il gate ora esegue anche il vero worker BeatNet
sugli stessi stems senza batteria, sincronizzato a ogni hop e senza backlog.
Dodici secondi per caso, seed 143:

- 52 senza pad: ingresso 5.184 s (entro 9.231), ma 103.927 BPM, fit
  0.003/0.833: ottava errata con evidenza internamente coerente.
- 52 con pad: ingresso 10.037 s e 147.282 BPM: FAIL.
- 100 senza pad: ingresso 2.347 s (entro 4.8), 101.940 BPM: PASS.
- 100 con pad: ingresso 5.099 s, 60.685 BPM: FAIL.
- 168 senza pad: ingresso 2.309 s, ma 91.585 BPM: FAIL.
- 168 con pad: ingresso 3.568 s, 83.565 BPM: FAIL.

Quindi una nuova sorgente di onset generica non è una correzione sicura: sui
due estremi il modello vede una griglia buona precisamente all'ottava sbagliata.
La prova deliberata con soli due cambi armonici conferma il rischio: il pad
raggiunge temporaneamente 200 BPM e la batteria sola coerenza 1.0. Le protezioni
sono state ripristinate (warm-up 4 s, otto cambi, tonalShare lento); nessuna di
quelle soglie sperimentali è rimasta nel codice.

La referenza armonica matura riconosce 168 come 169.492 BPM dopo 16.427 s, ma a
52 non produce una fase valida neppure nella finestra estesa. Non può dunque
essere usata come correzione generale, e applicarla a parte già entrata
violerebbe inoltre la regola di non cambiare ottava mentre suona.

La strumentazione e i controlli negativi sono pronti, ma lo step NON è
completato. Gli otto cambi richiesti restano incompatibili con due battute
quando c'è un solo accordo per battuta. Se il test reale senza percussioni
conferma l'errore, serve un modello di beat/downbeat addestrato o calibrato anche
su accompagnamenti tonali; non un altro peak picker, che non può distinguere
52/104 e 84/168 su questi segnali radi. Fino ad allora attendere/TAP o i
controlli ÷2/×2 restano la risposta deterministica ai casi acusticamente
ambigui. L'obiettivo entro due battute resta aperto per l'armonia rada: non
abbassare il numero di cambi alla cieca, né attivare il percorso acustico non
verificato.

## Checkpoint registrazione live — 08/09/2026

File esterno al repository:
`/Users/nicolamarogna/Desktop/3 INFINITO.mp3` (296.908 s, stereo, 44.1 kHz,
128 kbit/s). `VPLive` in questa build non apre MP3; la conversione e gli
estratti WAV sono stati creati soltanto in `/tmp` con `mpg123`. L'originale non
e' stato modificato. Build e prove mirate, nessuna suite completa e nessun
commit:

```bash
cmake --build build-host --target VPLive -j4
/opt/homebrew/bin/mpg123 -q -w /tmp/vp-infinito.wav \
  '/Users/nicolamarogna/Desktop/3 INFINITO.mp3'
./build-host/VPLive_artefacts/Release/VPLive \
  --mix /tmp/vp-infinito.wav --auto --trace
./build-host/VPLive_artefacts/Release/VPLive \
  --mix /tmp/vp-infinito.wav --bpm 91 --trace
```

Risultato sul file intero: il brano parte intorno a 72 BPM, raggiunge circa
91 BPM verso 14-18 s, resta poi stabile e rallenta nel finale verso 84-82 BPM.
Il passaggio `--bpm 91` sull'intero file misura 90.54 BPM medi e 23.4 ms di
deriva media, ma il massimo di 571.8 ms NON e' attribuibile al follower: il
riferimento costante e' incompatibile con intro e finale variabili.

La modalita' `--auto` trova 3274 attacchi, 11.0/s. A 91 BPM i sedicesimi sono
solo 6.07/s: la referenza comprende note e rumore fuori griglia. I suoi 46.7 ms
di dispersione e il 29% entro 20 ms non sono quindi una misura valida della
fase o degli attacchi audio. Non usare questi numeri per regolare il PLL. Per
misurare la fase assoluta di questo brano serve una beat-grid annotata oppure
stems/click isolati; `--bpm` misura soltanto la deriva dopo avere rimosso
l'offset mediano.

Estratto centrale 30-250 s, dove 91 BPM e' un riferimento ragionevole:

```bash
/opt/homebrew/bin/mpg123 -q -k 1148 -n 8422 \
  -w /tmp/vp-infinito-center.wav \
  '/Users/nicolamarogna/Desktop/3 INFINITO.mp3'
./build-host/VPLive_artefacts/Release/VPLive \
  --mix /tmp/vp-infinito-center.wav --bpm 91 --trace
```

Risultato: 90.99 BPM medi (scarto 0.01%), deriva media 8.5 ms, massimo 89.7 ms.
Non appare deriva progressiva. Da avvio freddo il clock pubblica 95.57 BPM a
2 s, 95.43 a 4 s, 92.31 a 6 s e 91.45 a 8 s; entra a 2 s e dichiara `FISSO`
intorno a 14 s. E' utilizzabile vicino al limite di due battute a 91 BPM
(5.27 s), ma la certezza piena e' ancora troppo lenta.

### Regressione dipendente dal livello — riproducibile

Stesso estratto 30-120 s, senza cambiare il contenuto musicale:

```bash
/opt/homebrew/bin/mpg123 -q -f 8192 -k 1148 -n 3445 \
  -w /tmp/vp-infinito-quiet.wav \
  '/Users/nicolamarogna/Desktop/3 INFINITO.mp3'
/opt/homebrew/bin/mpg123 -q -f 65536 -k 1148 -n 3445 \
  -w /tmp/vp-infinito-hot.wav \
  '/Users/nicolamarogna/Desktop/3 INFINITO.mp3'
./build-host/VPLive_artefacts/Release/VPLive \
  --mix /tmp/vp-infinito-quiet.wav --bpm 91 --trace
./build-host/VPLive_artefacts/Release/VPLive \
  --mix /tmp/vp-infinito-hot.wav --bpm 91
```

- Livello basso (`-f 8192`, ampiezza 0.25, circa -12 dB): il decoder parte a
  212.61 BPM; clock 202.27 a 2 s, circa 208-211 fino a 10 s, poi snap a 91.19
  a 12 s. `FISSO` soltanto verso 20 s. Media della finestra 94.94 BPM, deriva
  media 15.7 ms, massimo 257.7 ms. **Fallisce chiaramente le due battute.**
- Livello alto (`-f 65536`, con clipping possibile): 91.06 BPM medi, deriva
  media 5.3 ms, massimo 19.0 ms. Nessuna perdita di tempo osservata.

Il difetto confermato da questa registrazione non e' quindi "volume alto fa
perdere il tempo", ma **il livello basso consente una falsa ottava alta durante
l'acquisizione iniziale**. Una volta stabilizzato a 91 BPM il mantenimento e'
buono. Non e' ancora noto se la causa primaria sia il preprocessing del tensore
BeatNet, la soglia delle attivazioni o l'arbitraggio iniziale del decoder.

## Lavoro ancora necessario, in ordine

1. **Rendere BeatNet indipendente dal livello prima di cambiare il PLL.**
   Strumentare solo il probe/offline path e confrontare, nei primi 12 s, RMS e
   picco del segnale, range del tensore inviato a ONNX e attivazioni beat per
   originale, -6, -12 e -18 dB. Verificare che il frontend applichi esattamente
   la normalizzazione usata in addestramento. Non inserire AGC nel bus audio;
   un eventuale condizionamento deve riguardare esclusivamente la copia del
   segnale destinata al modello.
2. **Correggere l'arbitraggio iniziale 91/182-212.** Nel caso debole il pettine
   vede gia' circa 91 BPM a 8-10 s mentre la rete pubblica circa 208. Prima di
   pubblicare o mantenere un'ottava estrema, usare evidenza fresca e concordante
   tra le sorgenti gia' esistenti. Non aggiungere un altro onset picker e non
   riavviare il clock. Conservare la precedenza dei cambi di tempo confermati.
3. **Aggiungere una regressione mirata del livello.** Sweep almeno -18/-12/-6/
   0 dB e caso clippato, su 52/91-100/168 BPM; misurare separatamente tempo alla
   prima ottava corretta, ingresso delle percussioni, `FISSO`, deriva e recupero.
   Obiettivo sul caso riconoscibile: ottava e ingresso entro due battute, poi
   stabilita' per almeno due beat. Eseguire solo il nuovo filtro di test.
4. **Ottenere una verita' di fase per la registrazione.** Annotare manualmente
   una breve beat-grid nelle sezioni 30-120, 120-210 e 210-250 s, oppure ottenere
   uno stem/click affidabile. Solo allora misurare clock e attacchi audio assoluti
   ed esaminare i picchi isolati da 85-167 ms osservati nei tagli a freddo.
5. **Completare il caso senza batteria.** Provare stems o una registrazione in
   cui la pulsazione tonale sia isolabile. Il test armonico sintetico con pad e'
   ancora fallito e la fonte armonica richiede troppo tempo a 52 BPM. Se BeatNet
   non generalizza, valutare un modello beat/downbeat adatto a materiale tonale;
   non indebolire alla cieca warm-up, otto cambi o `tonalShare`.
6. **Verificare infine su iPad**, includendo variazione del gain hardware,
   attivazione/disattivazione delle percussioni e ritorno acustico. Il probe host
   non simula microfono, stanza, latenza I/O o feedback degli strumenti.

Lo step 5 resta aperto. Criterio di chiusura: su registrazioni con pulsazione
riconoscibile, ottava corretta e ingresso entro due battute; dopo un rientro,
almeno due beat stabili. Riportare separatamente sintetico, file reale annotato
e prova iPad. Dopo ogni correzione aggiornare questo file con comando, misura e
limite; non creare commit finche' l'utente non lo richiede.

## Punto 1 — livello e frontend BeatNet (08/09/2026)

Strumentato **solo il percorso probe/offline**: `scripts/probe_activations.cpp`
prende ora `--wav file.wav --sweep [--secs 12] [--gains 0,-6,-12,-18]` e stampa,
per ogni guadagno, RMS e picco del segnale, l'intervallo del tensore inviato a
ONNX (metà magnitudine e metà differenza separate) e le attivazioni del modello.
Il guadagno è applicato **soltanto alla copia destinata al modello**; nessuna
AGC nel bus audio, nessuna modifica al codice di produzione. Nessun commit.

```bash
cmake --build build-host --target VPActivations -j4
./build-host/VPActivations_artefacts/Release/VPActivations \
  --wav /tmp/vp-infinito-30-120.wav --sweep --secs 12 --gains 0,-6,-12,-18
```

Estratto 30-120 s della registrazione dell'utente, primi 12 s, 596 fotogrammi
per riga, ONNX reale:

| gain | rmsDb | peakDb | magMax | magMean | diffMean | pBeat medio | pBeat>0.5 | pDown medio | pDown>0.5 |
|---|---|---|---|---|---|---|---|---|---|
| 0 | -19.5 | -4.0 | 2.0804 | 0.2395 | 0.0301 | 0.0681 | 33 | 0.0352 | 17 |
| -6 | -25.5 | -10.0 | 1.7839 | 0.1595 | 0.0216 | 0.0785 | 23 | 0.0840 | 30 |
| -12 | -31.5 | -16.0 | 1.4910 | 0.1020 | 0.0148 | 0.0764 | 15 | 0.1441 | 57 |
| -18 | -37.5 | -22.0 | 1.2047 | 0.0624 | 0.0096 | 0.0690 | 3 | 0.1685 | 67 |

Nessun campione clippato in nessuna riga. La media di `pBeat` è quasi costante e
quindi **non è una misura utile**; quello che cambia è la forma: i fotogrammi di
battito confidenti scendono da 33 a 3, mentre le attivazioni di downbeat sopra
0.5 salgono da 17 a 67. A -12 dB il modello riceve una metà-differenza (la
feature di attacco) ridotta a circa la metà, e restituisce un'evidenza
qualitativamente diversa, non semplicemente più debole.

### Il frontend applica la normalizzazione dell'addestramento

BeatNet usa `LOG_SPECT` di madmom a 22050 Hz, finestra 1411, hop 441, 24
bande/ottava 30-17000 Hz, filtri a somma 1, `log10(1 + x)` — gli stessi valori
già in `BeatModelConfig.h`. madmom scala la finestra per `1/iinfo(int16).max`
sui file interi, quindi il modello vede campioni in ±1 con FFT **non**
normalizzata.

Verifica numerica con un seno a 1 kHz a fondo scala, 22050 Hz (WAV generato
nello scratchpad, `--sweep --secs 3`):

- riferimento madmom analitico: `|X|` di picco `352.1` (atteso `A·N/4 = 352.75`)
- il nostro frontend: `magMax = 2.2666`, cioè `log10(1 + 183.6)`

`183.6 / 352.1 = 0.52` è esattamente il peso che il filtro triangolare a somma 1
dà al bin di picco. Scala, finestra e filterbank coincidono. Un errore di
normalizzazione plausibile darebbe ordini di grandezza diversi: FFT divisa per N
darebbe circa `0.05`, campioni in scala int16 darebbero circa `6.8`.

**Limite:** madmom non è installato su questa macchina (richiede numpy < 2), e
non è stato quindi fatto un confronto fotogramma per fotogramma con
l'implementazione originale. Il controllo sopra verifica la scala e la catena,
non ogni dettaglio del filterbank.

Lo stesso seno scende da `magMax` 2.2666 a 1.3826 a -18 dB: la dipendenza dal
livello è **intrinseca a `log10(1 + x)`**, non un difetto della nostra porta.
BeatNet ha la stessa proprietà; è stato addestrato su musica a livello naturale,
quindi un ingresso a -12 dB è fuori dalla distribuzione di addestramento.

### Conseguenza per i punti successivi

Il punto 1 è **misurato e chiuso come diagnosi**: non c'è una normalizzazione da
correggere nel frontend. Restano due vie, in quest'ordine:

1. Il punto 2 (arbitraggio iniziale) resta necessario e indipendente dal livello:
   con evidenza povera non si deve pubblicare né mantenere un'ottava estrema.
2. Solo dopo, se serve, un condizionamento lento della **sola copia del segnale
   destinata al modello**, che porti l'RMS verso il livello dell'addestramento.
   Non è stato implementato qui: con un file è tautologico (riporta esattamente
   la riga a 0 dB) e va misurato con una costante di tempo reale e la
   regressione del punto 3, non con un guadagno costante.

Prossima azione: punto 2, l'arbitraggio iniziale 91/182-212.

## Punto 2 — arbitraggio iniziale 91/182-212 (08/09/2026)

### Causa trovata

Strumentando i quattro rami di acquisizione di `BeatDecoder::updateTempo`
(printf temporanei, rimossi):

```
quiet (-12 dB): ACQ fast bpm=212.04 raw=424.08 hmm=103.45 margin=0.047 self=1 comb(ready=0)
hot  (+6 dB):   ACQ fast bpm=94.07  raw=188.15 hmm=90.91  margin=1.695 self=1 comb(ready=0)
```

Il ramo è `tryFastAcquire`. Nel caso debole i picchi rilevati distano 141 ms
(424 BPM). Il test di alternanza forte-debole-forte conclude — correttamente —
«questa spaziatura è una suddivisione» e raddoppia il periodo una volta: 212.
Ma 212 è ancora più veloce di qualsiasi pulsazione del brano e supera lo stesso
test, e nessun ramo lo ripiega di nuovo.

Il punto vero: i rami a coppie e ad alternanza impostano `bestError = 0.0f`,
che **disattiva** il controllo `bestError > kFastAcquireMaxLevelError` contro
lo state space. È voluto (serve perché gli ottavi forti a 76 BPM non debbano
discutere con un prior ancora a 118), ma in cima al range regala il clock
all'unica lettura che solo il pettine può smentire — e il pettine tace fino a
circa 8 s. Lo state space diceva **103.45**, un'ottava esatta di distanza, e
nessuno lo guardava. `margin` era 0.047: non era sicuro, ma non era muto.

### Correzione

`Source/AI/BeatDecoder.cpp`, `tryFastAcquire`, 4 righe più commento: sopra
`kFastAcquireVetoBpm = 180.0f` lo state space conserva un veto — se nomina un
livello oltre `kOctaveThreshold` (0.25 ottave) dal candidato, non si pubblica.
Sotto la banda nulla cambia: 168 aggancia ancora sul solo intervallo. Nessun
nuovo onset picker, nessun restart del clock, precedenza dei cambi confermati
intatta.

Aggiunto anche `VPLive --gain <dB>`, che scala il mix prima dell'analisi: la
prova di livello diventa un ciclo su un solo file, senza pre-renderizzare WAV.

### Misure

```bash
cmake --build build-host --target VPLive -j4
for g in 0 -6 -12 -18; do ./build-host/VPLive_artefacts/Release/VPLive \
  --mix /tmp/vp-infinito-30-120.wav --bpm 91 --gain $g; done
```

| livello | BPM medio prima | dopo | deriva media prima → dopo | peggiore prima → dopo |
|---|---|---|---|---|
| clippato | 91.06 | 91.07 | 5.3 → 5.3 ms | 19.0 → 18.8 ms |
| 0 dB | 91.01 | 91.01 | 10.0 → 10.0 ms | 85.4 → 85.5 ms |
| -6 | 91.01 | 91.01 | 11.8 → 11.9 ms | 93.7 → 93.8 ms |
| **-12** | **94.92 (+4.3%)** | **90.96 (+0.04%)** | **16.3 → 9.5 ms** | **262.5 → 139.4 ms** |
| -18 | 121.10 | 121.06 | 61.7 → 63.3 ms | 304.8 → 322.7 ms |

Traccia a -12 dB dopo la correzione: pubblica 90.05 a t=2, suona da t=4, non
tocca mai 212. Prima: 212.61 a t=2, snap a 91.19 solo a t=12, `FISSO` a ~20 s.

### Regressioni, tutte eseguite prima/dopo

- `probe_matrix` (12 stili × 5 tempi × 6 semi = 360 corse): output **identico
  byte per byte**. Aggancio medio 5.25 s, 101 uscite, 30 mai-agganciate,
  9.01% fuori — invariati. Il banco arriva a 170 BPM, quindi il veto non scatta
  mai lì: è la prova che non tocca il materiale normale.
- `probe_tempo_step`: **identico**.
- `VPAlign`: **identico**.
- `VPTests --tempo-slow`: 10 PASS.
- `VPTests --octave`: 7 PASS / 4 FAIL, **identici prima e dopo**. I quattro
  fallimenti sono i casi a 50 BPM dell'item 1 di `docs/TODO.md`, aperti da
  prima e non toccati da questa modifica.

Nessuna suite completa. Nessun commit.

### Limiti

- **-18 dB non è risolto** ed è un guasto diverso: aggancia correttamente 91
  entro t=12, poi a t=14 è il **pettine stesso** a saltare a 182.37 e a
  trascinare tutto per quattordici secondi, prima di tornare a 91 a t=28. È un
  salto d'ottava dopo l'acquisizione, con le sorgenti concordi sul valore
  sbagliato; il veto di acquisizione non lo tocca. Invariato prima/dopo.
- **Nessun test automatico blocca ancora questo veto.** Tentato un sintetico a
  livello di decoder (treno a 428 BPM più polso a 103): non riproduce il ramo,
  il peak picker sceglie il polso e la lettura resta corretta anche col codice
  vecchio. Il caso nasce dalla curva di attivazione di un feed reale silenzioso,
  non da un treno pulito. La verifica ripetibile è oggi il ciclo `--gain` sopra:
  il lucchetto automatico appartiene al punto 3.
- La soglia di 180 BPM è una banda, non una misura del brano: sopra i 180 reali
  l'aggancio ora richiede che lo state space concordi, quindi su musica
  genuinamente velocissima l'ingresso può arrivare più tardi. Nessun banco del
  repository copre quella zona.

Prossima azione: punto 3, la regressione mirata del livello, che ora può usare
`VPLive --gain` e deve misurare separatamente prima ottava corretta, ingresso,
`FISSO`, deriva e recupero su 52 / 91-100 / 168 BPM.

## Punto 3 — regressione mirata del livello (08/09/2026)

Nuovo filtro `VPTests --level [52|91|168]`, in `Tests/TestAiBeat.cpp`
(`vpRunLevelSweepTest`). Kit sintetico normalizzato a picco 0.9, poi 0 / -6 /
-12 / -18 dB più un caso clippato (×4 e taglio a ±1), su 52 / 91 / 168 BPM.
38 s per corsa: musica da 1 s, vuoto di 2 s a 26 s, rientro fino a 38 s.
Il guadagno è applicato **all'ingresso**, non al bus di analisi: è quello che
una sorgente più bassa è davvero, e non introduce il condizionamento che il
punto 1 ha lasciato per dopo.

Cinque numeri separati per corsa, come chiede il punto 3 — un'ottava sbagliata,
un ingresso tardivo e un clock che deriva sono tre guasti diversi:

- **ottava**: primo istante in cui `|log2(bpm/vero)| < 0.25` e ci resta due beat
  (stessa domanda di `kOctaveThreshold`, posta da fuori);
- **ingresso**: `percussionAudible`;
- **FISSO**: `tempoRegime == fixed`;
- **fase**: errore **segnato** medio su 20-26 s, meno `attackLeadMs`;
- **rientro**: dopo il vuoto, ottava di nuovo giusta per due beat.

Più `bpm@4s`, che dice *su quale* livello sbagliato stava.

```bash
cmake --build build-host --target VPTests -j4
./build-host/VPTests_artefacts/Release/VPTests --level        # 4:46, 13 PASS / 3 FAIL
./build-host/VPTests_artefacts/Release/VPTests --level 91     # un tempo solo
```

Il filtro **non** è nella suite completa: quindici corse sono circa cinque
minuti. Va lanciato a mano dopo ogni modifica al livello di analisi, al frontend
o alla logica d'ottava. Due corse consecutive danno numeri identici.

### Tabella (tree attuale, con il veto del punto 2)

```
52 BPM   due battute = 9.23 s
livello  picco  ottava   ingresso  FISSO    fase   rientro  bpm@4s   bpm
  0 dB   0.900    -1.00      1.30   22.70     n/a    -1.00  105.83  104.01
 -6 dB   0.451    -1.00      1.30   22.12     n/a    -1.00  105.83  103.97
-12 dB   0.226    -1.00      1.30   22.12     n/a    -1.00  107.23  103.86
-18 dB   0.113    -1.00      1.87   23.24     n/a    -1.00  104.55  103.83
clip     1.000    -1.00      0.99   -1.00     n/a    -1.00   83.07  104.11

91 BPM   due battute = 5.27 s
  0 dB   0.900    17.71      1.88   -1.00     2.8     0.01   66.67   91.07
 -6 dB   0.451     2.53      2.93   20.16    27.7     0.01   91.22   91.05
-12 dB   0.226     3.35      2.78   -1.00    32.6     0.01  106.14   90.46
-18 dB   0.113     2.51      2.53   18.14    26.1     0.01   91.01   91.05
clip     1.000     1.21      1.61    8.28    13.3     0.01   91.00   91.01

168 BPM  due battute = 2.86 s
  0 dB   0.900     0.29      0.41   10.84    -2.2     0.01  168.71  168.00
 -6 dB   0.451     0.29      0.41   11.20     6.7     0.01  168.15  168.05
-12 dB   0.226     0.29      0.41   10.84     6.5     0.01  168.56  168.02
-18 dB   0.113     0.29      0.41   10.84     6.5     0.01  168.64  168.02
clip     1.000    -1.00      0.41   22.98     n/a    -1.00  168.41   84.04
```

### Cosa dice

1. **168 BPM è pulito a ogni livello pulito**: ottava a 0.29 s, ingresso a
   0.41 s, fase 6.5 ms, da 0 a -18 dB. Il livello, da solo, non rompe niente.
2. **A 91 BPM il -12 dB ora aggancia in 3.35 s**, dentro le due battute: è la
   correzione del punto 2 vista dal banco sintetico.
3. **52 BPM legge 104 a ogni livello.** È l'item 1 di `docs/TODO.md` — un kit
   con gli hat sugli ottavi a 52 e un kit half-time a 104 sono lo stesso
   segnale — e non dipende dal guadagno. **Misurato e non asserito**: metterci
   un gate renderebbe questo filtro rosso per un motivo che non sta misurando.
4. **Tre fallimenti veri, aperti:**
   - **91 BPM a 0 dB: 17.71 s per l'ottava**, mentre -6/-12/-18 ci arrivano in
     2.5-3.4 s. `bpm@4s = 66.67`: sta **sotto**, non sopra. È la riga più forte
     a essere l'anomalia. Provato a spostare il riferimento da 0.9 a 0.7 di
     picco per escludere `kMakeupClipGuardPeak`: **numero identico**, 17.71 s.
     Non è la guardia di clipping.
   - **168 BPM clippato: si assesta a 84**, la metà, e non rientra dopo il
     vuoto. A 4 s stava ancora leggendo 168.41, quindi l'ottava la perde dopo,
     non in acquisizione.
   - Il rientro a 168 clippato fallisce come conseguenza del precedente.
5. **`FISSO` è erratico**: mai raggiunto in 26 s su cinque righe che per tutto
   il resto sono corrette (91 a 0 dB e -12 dB, 52 clippato). Non è una
   regressione di questo lavoro; è un dato nuovo che questo banco rende visibile.
6. **La fase peggiora quando il livello scende, a 91 BPM**: 2.8 ms a 0 dB contro
   26-33 ms a -6/-12/-18. A 168 BPM non succede (6.5 ms ovunque). Un ritardo di
   circa 30 ms non è l'ottava e non si vede nelle colonne dell'aggancio.

### Limiti

- Materiale **sintetico**. Non riproduce l'errore che ha originato l'item 24
  (212 BPM a -12 dB su registrazione reale): quello nasce dalla curva di
  attivazione di un feed reale silenzioso. La prova su file reale resta
  `VPLive --gain`, ed è quella che ha misurato la correzione del punto 2.
- Il caso clippato è ×4 con taglio duro: è una caricatura di un preamp in
  saturazione, non la compressione di un banco vero.
- Nessuna suite completa eseguita. Nessun commit.

Prossima azione: punto 4, la verità di fase annotata sulla registrazione — senza
la quale i 26-33 ms del punto 6 qui sopra non si possono confrontare con nulla di
reale.

## Stato a fine sessione — 08/09/2026

Punti 1, 2 e 3 chiusi; il lavoro è nel commit `dcff368` (fatto dall'utente, in
un blocco unico) sopra `e5cc06e`. iPad provato dall'utente sulla nuova build:
«sembra ok». È un giudizio, non una misura — non sono stati registrati livello,
tempo alla prima ottava corretta, ingresso né deriva su dispositivo.

Restano aperti, in ordine di valore:

1. **Punto 4 — verità di fase annotata** sulla registrazione (30-120 / 120-210 /
   210-250 s), o uno stem/click affidabile. Senza, i 26-33 ms di ritardo che
   `--level` misura ai livelli bassi a 91 BPM non si possono confrontare con
   niente di reale, e i picchi isolati da 85-167 ms restano non attribuiti.
2. **I tre guasti che `VPTests --level` ha trovato** (docs/TODO.md item 24):
   91 BPM a 0 dB impiega 17.71 s per l'ottava mentre tutti i livelli più bassi
   ci arrivano in 2.5-3.4 s; 168 BPM clippato si assesta a 84 e non rientra
   dopo il vuoto; `FISSO` non arriva in 26 s su righe per il resto corrette.
3. **-18 dB**: aggancia 91 entro 12 s, poi è il **pettine** a saltare a 182.37
   per quattordici secondi. Salto d'ottava dopo l'acquisizione, con le sorgenti
   concordi sul valore sbagliato: il veto d'acquisizione non lo tocca.
4. **Punto 5 — il caso senza batteria.** Il test armonico con pad fallisce
   ancora e la fonte armonica è troppo lenta a 52 BPM. Se BeatNet non
   generalizza serve un modello beat/downbeat adatto a materiale tonale; non
   indebolire alla cieca warm-up, otto cambi o `tonalShare`.
5. **La suite completa non è mai stata eseguita in questa sessione**, per scelta
   dell'utente. Prima di una consegna va fatta girare almeno una volta.

Item 1 (52 BPM letto 104 a ogni livello) resta indecidibile su quel materiale e
non è un difetto di livello: `--level` lo misura e deliberatamente non lo asserisce.

## Punto 2 della lista aperta — i tre guasti trovati da `--level` (08/09/2026)

Diagnosi con `VP_LEVEL_TRACE=1` (nuovo, documentato in `Tests/TestAiBeat.h`) e
printf temporanei sui quattro rami di acquisizione, poi rimossi.

### A — 91 BPM a 0 dB: causa trovata, non corretta

```
VPACQ t=2.58 fast bpm=64.55 hmm=115.38 margin=-0.174 comb(r=0 s=0)   <- 0 dB, fallisce
VPACQ t=3.30 fast bpm=90.34 hmm= 96.77 margin= 6.071                 <- -6 dB
VPACQ t=1.98 fast bpm=90.38 hmm= 90.91 margin= 4.465                 <- clip
```

`tryFastAcquire` pubblica **64.55** mentre lo state space nomina **115.38** con
margine **negativo**: non e' incerto, e' contrario. E' la stessa forma del bug
del punto 2, ma sotto i 180 BPM, dove il veto non arriva. Qui a disattivare il
controllo di livello e' la regola `rawBpm < 90`, che sotto quella soglia prende
la spaziatura osservata per buona (`bestPeriod = raw`, `intervalSelfSufficient`)
per non ripetere l'errore 76 -> 152.

Il clock resta poi su tre quarti della pulsazione (67.7) mentre il pettine dice
90.91 dagli 8.5 s e non cambia idea.

**Non corretto di proposito.** Stringere la regola dei 90 su una sola riga
sintetica e' esattamente cio' che l'item 23 vieta («non tarare niente su un
brano solo»), e la stessa regola e' l'unica cosa che tiene 76 BPM lontano da
152. Serve materiale reale a piu' livelli prima di toccarla.

### La correzione generale che ne e' uscita

Le due concessioni di pazienza dello snap d'ottava - `kOctaveSnapBeatsHealthy` e
il bonus di anzianita' - si applicavano a **qualsiasi** disaccordo oltre 0.25
ottave. Esistono per l'ambiguita' d'ottava: una griglia doppia cade su ogni
battito rilevato, quindi sembra sempre sana, e scegliere fra i due livelli e'
davvero ambiguo. Un disaccordo che **non** e' vicino a un numero intero di
ottave non ha quella scusa: 67.7 e 90.9 non sono la stessa pulsazione contata
in due modi, una delle due e' semplicemente la griglia sbagliata.

Ora entrambe le concessioni sono subordinate a `octaveArgument`, cioe' alla
distanza fra `|log2(bpm/combRaw)|` e l'intero piu' vicino, entro
`kOctaveArgumentTolerance` (0.15). Quando il pettine e' d'accordo, o e' d'accordo
a meno di un'ottava esatta, la distanza e' quasi zero e tutto si comporta come
prima.

Misure prima/dopo, tutte eseguite:

- `probe_tempo_step`, colonna lenta: 120->150 **17.2 -> 12.8 s**; 100->160
  **13.5 -> 10.1**; 160->100 **15.0 -> 12.0**; 90->120 **26.7 -> 20.7**. La
  colonna rapida non si muove (0.8 / 1.2 / 1.7 s). Un salto di tempo e' proprio
  un disaccordo non-ottava: prima pagava la pazienza d'ottava per intero.
- `probe_matrix`, 360 corse: aggancio medio **5.25 -> 5.20 s**, uscite
  **101 -> 99**, fuori medio 9.01 -> 9.00%. `swing 8vi` **1/30 -> 0/30**,
  `swing pieno` **3/30 -> 2/30**. Nessuna riga peggiora.
- `VPAlign`: gradino 100->140 **15.14 -> 11.26 s**; le righe a 168 gia' marcate
  «livello sbagliato: -67%» si muovono nel rumore.
- `VPTests --octave`: 7 PASS / 4 FAIL, **identico** (sono i 50 BPM dell'item 1).
- `VPTests --tempo-slow`: 10 PASS.
- `VPTests --level`: 13 PASS / 3 FAIL; 91 a 0 dB **17.71 -> 15.67 s**, tutto il
  resto invariato.

### B — 168 BPM clippato: non e' l'arbitraggio, e' il pettine

```
TR t=14.00 bpm=167.78 neur=168.13 comb=168.07 conf=0.97 res=0.014 settled=1
TR t=15.00 bpm= 84.03 neur= 84.03 comb= 84.03 conf=0.54 res=1.000 settled=1
```

Quattordici secondi di 168 corretto e sano, poi **il pettine stesso** passa a
84.03 e il decoder segue una sorgente che ha cambiato idea. Il veto dell'item 22
(`unprovenSlowerOctave`) non si applica perche' `gridLooksLikeSubdivision` e'
vero: il kit alterna cassa e rullante, quindi le forze alternano e il pettine
nomina la meta' — che e' esattamente la firma per cui quel veto si fa da parte.

E' l'ambiguita' dell'item 1 (un kit a 168 con cassa/rullante alternati **e'** un
kit half-time a 84) fatta emergere dal clipping. Nota per chi ci torna: la
regola dell'item 17, «l'ottava non cambia mentre sta suonando», vive in
`BeatTracker::updateAutoOctave` e **non copre** uno snap del decoder — qui il
tempo si e' dimezzato sotto di essa con `suona=1`.

### C — `FISSO`: la mia etichetta era sbagliata

Non e' un terzo difetto. Guardando il regime a 20-26 s per livello, a 91 BPM:

| livello | regime | perche' |
|---|---|---|
| 0 dB | `unknown` | conseguenza di A: trova il livello solo a 15.67 s |
| -6 dB | **`fixed`** | ok |
| -12 dB | **`live`** | su un tempo costante decide che si muove |
| -18 dB | **`fixed`** | ok |
| clip | **`fixed`** | ok |

`mayFix` chiede `lastFitResidual < 0.05` e una finestra assestata: `FISSO` segue
la qualita' dell'aggancio. L'unica riga davvero curiosa e' -12 dB letta come
`live`, coerente con i suoi 32.6 ms di fase: la stessa degradazione che la
colonna «fase» misura, vista da un'altra parte.

Prossima azione: nessuna correzione ulteriore senza materiale reale a livelli
diversi. A e B restano aperti negli item di `docs/TODO.md`.

## Il pettine che cambia idea sotto una parte che suona — 08/09/2026

Due riproduzioni indipendenti dello stesso comportamento, da estremi opposti
dello sweep di livello:

```
--level 168, riga clip:
  TR t=14.00 bpm=167.78 neur=168.13 comb=168.07 conf=0.97 res=0.014 settled=1
  TR t=15.00 bpm= 84.03 neur= 84.03 comb= 84.03 conf=0.54 res=1.000 settled=1

VPLive --gain -18 sulla registrazione reale:
  t=12  orologio=92.16   decoder=91.84   pettine=91.46   residuo=0.134
  t=14  orologio=182.37  decoder=182.37  pettine=182.37  residuo=1.000
```

In entrambi la griglia era sana e assestata, e poi **il pettine stesso** ha
nominato l'ottava sbagliata e il decoder l'ha seguito. Le sorgenti sono
d'accordo fra loro sul valore sbagliato, quindi nessun arbitraggio fra loro può
aiutare: sbagliato è il **momento**, non l'evidenza.

### Dove mancava la regola

`BeatTracker::updateAutoOctave` rifiuta già di muovere il livello metrico sotto
una parte che suona, e spiega perché per quindici righe: dimezzare o raddoppiare
sotto un percussionista non è una correzione di tempo, è la griglia su cui sta
suonando che si sposta, e la densità della parte, dove cade la battuta e cosa
dice il display diventano sbagliati tutti insieme.

Quella regola poteva però valere **solo sullo shift del tracker**. Il decoder
può dimezzare il tempo che riporta, e allora `autoOctave` resta a zero mentre la
griglia si muove lo stesso: la protezione era un piano troppo in alto.

### Correzione

`sounding` arriva al decoder con lo stesso passaggio già usato per l'ottava
dell'utente e per `lineFeed`: `BeatTracker` → `NeuralBeatTracker` (atomica) →
`BeatDecoder`. Nel voto dello snap d'ottava:

```cpp
const bool levelHeldWhilePlaying = sounding && ! provisional && octaveArgument;
```

Solo un'argomentazione **sull'ottava** viene rifiutata, e solo quando il livello
ha smesso di essere provvisorio:

- un tempo che è davvero cambiato non è vicino a un numero intero di ottave e
  passa ancora (è la stessa `octaveArgument` introdotta poco sopra);
- un ingresso nuovo azzera `established` via `notifyInputRestart` e non è
  coperto da questa regola;
- finché il livello è `provisional` — circa i primi 8-11 s, fino a
  `levelSettled` — le correzioni funzionano come prima, il che lascia una
  valvola di sicurezza a un'acquisizione sbagliata;
- se il livello tenuto è quello sbagliato, la via d'uscita è quella che l'item
  17 nomina già: ÷2 / ×2.

### Misure

- `VPTests --level`: **15 PASS / 1 FAIL**, da 13/3. La riga 168 clippata passa
  da «si assesta a 84, nessun rientro» a ottava 0.29 s, ingresso 0.41 s, finale
  **168.03**, fase 2.6 ms, rientro 0.01 s. A 168 BPM ora **8 PASS / 0 FAIL**.
  Nessuna riga peggiora; a 52 BPM tutto invariato.
- `VPLive --gain -18` sulla registrazione: BPM medio **121.06 → 91.08**
  (scarto 33% → 0.09%), e la traccia non tocca più 182. I livelli 0 / -6 / -12
  sono identici al decimo di millisecondo.
- `VPTests --octave`: 7 PASS / 4 FAIL, **identico**, compresi i tasti ÷2 e ×2.
- `VPTests --tempo-slow`: 10 PASS. `--state-timing`: 3 PASS.
- `VPAlign`: **identico**.
- `probe_matrix` (360 corse) e `probe_tempo_step`: **identici**. Vale la pena
  dirlo per intero: quei due banchi pilotano `BeatDecoder` da soli e non
  chiamano mai `setSounding`, quindi `sounding` resta falso e la nuova regola
  non si attiva. Non possono regredire per questa modifica **e non possono
  neanche validarla**: la copertura viene da `--level`, `--octave` e `VPLive`.

### Limiti

- Una sezione **half-time vera dentro il brano** è un cambio d'ottava sotto una
  parte che suona, e adesso viene rifiutato. È esattamente lo scambio che
  l'item 17 aveva già accettato per il tracker; qui vale anche per il decoder.
  ÷2 / ×2 restano la via manuale.
- A -18 dB la deriva media resta **61.6 ms** (max 349.3): il tempo ora è giusto
  per tutta la corsa, ma la fase a quel livello è comunque povera. È lo stesso
  degrado che la colonna «fase» di `--level` misura, non l'ottava.
- Resta aperto il caso A (91 BPM a 0 dB, `tryFastAcquire` che pubblica 64.55
  contro uno state space a 115.38 con margine negativo): non è un'ottava, quindi
  questa regola non lo tocca, ed è giusto così.
- **`VPTests --bar` fallisce** («within two bars of the return the one is beat
  zero again»), e falliva già a `dcff368` e prima delle modifiche di oggi:
  verificato ricostruendo entrambi. Non è una regressione di questo lavoro, ma
  non risulta annotato da nessuna parte — l'item 2 di `docs/TODO.md` è segnato
  chiuso.

## `VPTests --bar`: il test era sbagliato, non la battuta — 08/09/2026

Il fallimento («within two bars of the return the one is beat zero again») e'
piu' vecchio di tutto il lavoro di oggi: identico a `e5cc06e`, `11618cb`,
`aa97ced`, `9fcaf02` e `dcff368`, ricostruiti uno per uno. Deterministico su
tre corse.

**Causa.** L'asserzione pretendeva `beatAfter == 0` alla lettera. Ma
`barPhase * 4` e' la posizione nella battuta **all'istante in cui si guarda**, e
lo stub `ShiftBarModel` mette l'uno dove `(beatNo + 2) % 4 == 0`. Quello zero era
vero finche' la finestra di recupero era `fourBarsSec + 1.0`: 27.797 s cade sul
battito 46, e 46 + 2 e' multiplo di quattro. Il commit `74fe5fc` ha stretto la
finestra a `twoBarsSec` senza ricontrollare dove si andava a cadere: 22.0 s cade
sul battito 36, e il conteggio legge 2 perche' **2 e' dove l'uno adesso sta**.

**Prova che il codice e' giusto.** Con la finestra resa variabile, a sei
lunghezze diverse:

```
recover=4.8    t=22.0000 beatNo=36 atteso=2 letto=2
recover=6.0    t=23.2000 beatNo=38 atteso=0 letto=0
recover=7.2    t=24.4000 beatNo=40 atteso=2 letto=2
recover=8.4    t=25.6000 beatNo=42 atteso=0 letto=0
recover=9.6    t=26.8000 beatNo=44 atteso=2 letto=2
recover=10.6   t=27.7973 beatNo=46 atteso=0 letto=0
```

Sei istanti su sei. Il rientro della battuta funziona; quello che l'asserzione
verificava era la propria aritmetica.

**Correzione**: il test calcola `beatWanted = (beatNo + 2) % 4` dall'istante in
cui guarda, esattamente come fa gia' il test gemello `bar-seek` due blocchi piu'
sotto (`const int expected = ((fileBeat - 2) % 4 + 4) % 4;`). `VPTests --bar`:
**10 PASS / 0 FAIL**, tre corse identiche. Nessun codice di produzione toccato.

**E l'uno che sembrava non allinearsi da solo.** Prima del buco il conteggio
legge 3 dove l'uno del modello e' su 2, con `rot=0`. Avevo scritto che non si
allineava mai da solo: **e' sbagliato, si allinea**, solo molto piu' tardi di
quei sedici secondi.

Tracciando `tryAlignFrom`, l'opinione e' schiacciante fin dall'inizio -
`best=1 bestV=0.92 runner=0.03` - e a fermarla e' una cosa sola: `evid`, cioe'
`voteBeats`, non arriva mai a `kBeatsToMoveTheBar`. Spostando il buco:

```
cut=16 s   t=16.00 beatNo=26 l'uno e' su 2  letto=3  rot=0
cut=28 s   t=28.00 beatNo=46 l'uno e' su 2  letto=3  rot=0
cut=32 s   t=32.00 beatNo=53 l'uno e' su 1  letto=1  rot=1
cut=40 s   t=40.00 beatNo=66 l'uno e' su 2  letto=2  rot=1
```

La rotazione cade fra i **46.7 e i 53.3 battiti**, ed e' esattamente il conto:
`voteBeats = voteBeats * kVoteDecay + 1` con `kVoteDecay = 0.982` converge a
55.6 e attraversa 32 al **quarantasettesimo battito** - dodici battute,
ventinove secondi a 100 BPM, cinquantanove a 50.

Quindi non e' un difetto: e' il prezzo, voluto, di spostare una battuta che
l'ascoltatore sente. Quello che mancava era che il numero non fosse scritto da
nessuna parte - «32» non dice «dodici battute». Ora e' annotato accanto alla
costante, con la misura. Resta una domanda per l'utente, non un bug: se l'app
entra sull'uno sbagliato, ci mette mezzo minuto a correggersi da sola (e il
tasto «SPOSTA L'1» e' li' per quello).

Il test stampa ora anche dove l'uno **e'** prima del buco, cosi' quella riga si
legge senza rifare il conto.

## Follow-up recupero — 09/09/2026

Priorità utente: input brano e mixer; microfono esterno fuori dal lavoro attuale.
Nessun commit. Modifiche pregresse di Claude preservate.

- TempoFollower: minimo di correzione dopo conferma ridotto da 0.5 a 0.25 beat;
  durata ancora proporzionale allo scarto / rail 20%, conferma e protezioni intatte.
  Memoria del filtro ordinario aggiornata anche durante transizioni BPM rapide.
- probe_small_steps: allineate selezione del payload e tau rapido a BeatTracker;
  non è comunque un test end-to-end. Il precedente conteggio 8/3 sotto è errato:
  la verifica riproduce 7 PASS / 4 FAIL.
- probe_recovery: 84 PASS, gate suddivisioni rafforzato a 0.35 beat dopo conferma.
  A 120, passaggio spostato di 0.075 beat, buffer 256: 0.747 -> 0.624 s fino a
  rientro stabile entro 8 ms, inclusa conferma; nessuna attivazione nei 18 controlli
  negativi. Suddivisioni 52/100/168: totale 1.445/0.768/0.448 s.

Comandi mirati (nessuna suite completa):
```
c++ -std=c++17 -O2 -ISource scripts/probe_recovery.cpp Source/Tracking/TempoFollower.cpp -o /tmp/vp-reentry-clock
/tmp/vp-reentry-clock
c++ -std=c++17 -O2 -ISource scripts/probe_small_steps.cpp Source/AI/BeatDecoder.cpp Source/AI/TempoEstimator.cpp Source/AI/BeatHmm.cpp Source/Tracking/TempoFollower.cpp -o /tmp/vp-reentry-step
/tmp/vp-reentry-step
```

Incompleto: riconoscimento dei cambi piccoli (tre intervalli prima della conferma),
52->50 e cambi ±12 nel probe restano fuori soglia; non attribuirli tutti al clock.
Prossima azione: isolare pubblicazione del decoder e deriva di fase sul percorso
diretto nei casi lenti, con traccia dall'inizio dello scarto. Poi test su registrazione
reale con timestamp del problema e attacchi renderizzati. Nessuna garanzia sul mixer
fisico e nessuna nuova verifica microfonica in questo step.

## Gradini piccoli di tempo: completato il lavoro di astra — 08/09/2026

Richiesta dell'utente: *«far seguire e tenere ancora meglio qualsiasi tempo,
anche con un cambio di un paio di BPM improvviso»*, tempo trovato entro circa
due quarti e rientro immediato dalla deriva.

Nell'albero c'era il gate di astra: dentro il regime `fixed`, su feed diretto,
due fit di quattro battiti (`fitPeriodBefore` con `skipNewest`) che devono
essere entrambi puliti e consecutivi, il precedente ancora sul tempo committato
e la differenza sopra il rumore e sotto il 4%. Banco: `scripts/probe_small_steps.cpp`
(non ancora tracciato in git).

### Cosa mancava: dirlo al clock

Il gate riscriveva `bpm` e basta. Il clock arriva a un nuovo target con la sua
costante di tempo, quindi un gradino gia' **dimostrato** atterrava come una
pendenza. Ora lo stesso blocco pubblica una transizione `rapid` confermata - la
stessa pubblicazione del percorso dei salti grandi - e il regime resta `fixed`,
perche' un disco tagliato sul click che cambia di due BPM e' ancora un disco
tagliato sul click.

`probe_small_steps`: **da 6 PASS / 5 FAIL a 8 PASS / 3 FAIL**.

| caso | prima | dopo |
|---|---|---|
| 168 -> 170 | FAIL 2.977 s | **PASS 1.077 s** |
| 168 -> 166 | 1.317 s | **1.097 s** |
| 120 -> 122 | 1.720 s | **1.500 s** |
| 52 -> 54 | FAIL 5.975 s | **PASS 4.395 s** |
| 52 -> 50 | FAIL 6.515 s | 6.135 s, ancora FAIL |
| 120 -> 118 | PASS 2.000 s | FAIL 2.040 s (limite 2.034) |
| ±12 a 120 | FAIL | invariato: altro meccanismo |

Tracciando quando scatta: **3.1 battiti dopo il cambio**, a 52, 120 e 168, in
entrambi i sensi. E' il pavimento fisico - con un intervallo solo non si puo'
sapere che il tempo e' cambiato, ne servono due.

Regressioni: `probe_matrix` completo (360 corse) **identico byte per byte**,
`--steady` identico, `probe_tempo_step` identico, `VPAlign` identico,
`probe_recovery` PASS. Il banco usa il feed diretto, quindi il gate *poteva*
scattare e non e' mai scattato: su deriva musicale di 3 BPM al minuto non trova
mai due finestre pulite consecutive. Distingue un gradino da una deriva.

### La fase: ipotesi sbagliata, misurata e scartata

`beginTempoTransition` chiama `cancelPhaseRecovery()` sempre. Sembrava la causa
del ritardo residuo, e per un gradino piccolo lo scarto accumulato e' reale.
Provato a renderlo condizionato ai 3 BPM che quella funzione gia' usa per
azzerare il trim: **numeri identici, riga per riga.** Non era quello. Il codice
e' stato ripristinato; niente e' rimasto in albero.

### Dove vanno davvero i secondi

Traiettoria a 52 -> 50 (errore di fase contro la griglia vera):

```
t=+0.12  clock=51.999  err=  5.4 ms      <- il clock e' ancora sul vecchio tempo
t=+1.32  clock=51.999  err= 53.3 ms         e l'errore cresce, lineare
t=+3.42  clock=51.998  err=137.2 ms
t=+3.72  clock=50.000  err=132.7 ms      <- scatta il gate, il tempo e' giusto
t=+4.32  clock=50.000  err= 73.8 ms      <- la fase rientra, ~79% per battito
t=+4.92  clock=50.000  err= 28.4 ms
t=+6.12  clock=50.000  err= 25.0 ms      <- sotto i 25 ms
t=+6.72  clock=50.000  err= 11.4 ms      <- e li' si ferma
```

E la stessa cosa a 168 -> 170:

```
t=+0.06  clock=167.994 err= 1.0 ms
t=+1.06  clock=168.088 err=12.6 ms       <- non supera mai i 25 ms
t=+1.46  clock=170.006 err=12.3 ms       <- scatta il gate
```

**Non e' una costante di tempo da stringere, e' geometria.** La finestra di
rilevamento e' 3.1 *battiti*; l'errore che si accumula dentro quella finestra e'
3.1 battiti moltiplicati per l'errore relativo di tempo, quindi in millisecondi
cresce con il quadrato della durata del battito. A 168 BPM lo stesso gradino di
2 BPM accumula 12.6 ms e non si nota; a 52 BPM ne accumula 137 e poi vanno
camminati via. Dopo il gradino la fase rientra del ~79% per battito, che per un
anello che agisce sui battiti e' gia' vicino al suo limite.

Da qui: **a 120 e 168 BPM il rientro e' gia' immediato** (1.1-1.5 s, e la fase
non esce mai dai 25 ms). A 52 BPM servono circa cinque battiti in tutto, di cui
tre sono il minimo teorico per accorgersi del cambio.

### Limiti

- Il criterio di PASS del banco (stabile entro **quattro** battiti) e' sotto il
  pavimento fisico ai tempi lenti: 3.1 di rilevamento piu' circa due di fase
  fanno cinque. I due FAIL a ±2 BPM (52->50, e 120->118 che sbaglia di 6 ms)
  sono quello, non un difetto nuovo. Chi riprende decida se il criterio va
  scritto in battiti-dopo-il-rilevamento invece che in battiti-dal-cambio.
- I salti da ±12 BPM restano lenti (9.3 s a 132, e 108 non si stabilizza) e sono
  il percorso della transizione ordinaria, non questo gate.
- `scripts/probe_small_steps.cpp` non e' tracciato: va aggiunto a git.
- Il gate resta **solo su feed diretto** (`lineFeed`). Su microfono non e' mai
  stato provato e non deve esserlo senza una misura sua.

# RECAP — cosa serve per chiudere l'allineamento (09/09/2026)

Scritto su richiesta dell'utente come punto di ripartenza unico. Sostituisce la
necessità di leggere tutto quello che c'è sopra: sopra ci sono le misure, qui c'è
la mappa.

## Quattro problemi, non uno

«Non è allineato» in questo progetto vuol dire quattro cose diverse, che
falliscono per ragioni diverse e che è costato tempo confondere:

1. **Il tempo** — il BPM. Sbagliato = ottava sbagliata, o griglia sbagliata.
2. **La fase** — dove cade il quarto dentro il tempo giusto. Sbagliata = suona
   sistematicamente avanti o indietro di qualche decina di millisecondi.
3. **La battuta** — quale quarto è l'uno. Sbagliata = il tempo è giusto ma
   l'accento è sul posto sbagliato.
4. **L'ingresso** — quando la parte comincia a suonare. Sbagliato = entra su
   evidenza che non c'era.

Un banco che misura il primo non dice niente sugli altri tre. Metà degli errori
di lettura di questa sessione vengono da lì.

## Cosa è chiuso e misurato

| | stato | banco |
|---|---|---|
| Frontend BeatNet: scala, finestra, filterbank | verificato contro madmom, nessun bug | `VPActivations --sweep` |
| Ottava falsa alta in acquisizione (veto dello state space sopra 180 BPM) | corretto: -12 dB da 94.9 a 91.0 BPM | `VPLive --gain`, `--level` |
| Pazienza dello snap d'ottava su disaccordi non-ottava | corretto: `probe_tempo_step` −25%, `probe_matrix` uscite 101→99 | `probe_matrix`, `VPAlign` |
| Livello che cambia l'ottava sotto una parte che suona | corretto: `--level` da 13/3 a 15/1 | `VPTests --level` |
| `VPTests --bar` che falliva da settimane | era il test, non la battuta | `VPTests --bar` |
| Gradini piccoli (±2 BPM): il decoder lo dice al clock | rilevati a **3.1 battiti**, che è il minimo teorico | `probe_small_steps` |
| Barra e trim d'ingresso in dB, con banda bersaglio | corretto, visto su Mac e iPad | — |

## Cosa manca, in ordine di quanto pesa

### 1. L'app non sa dire «non lo so ancora» — è il problema più grande

Su `01 BLUE SKY.mp3` (riferimento, `/tmp/vp-bluesky.wav`): trenta secondi di
intro senza sezione ritmica, la rete produce risposte **confidenti e sbagliate**
(52, 57, 171, 120, 163, 133) e **l'app si impegna su una di quelle a 4 secondi**,
con confidenza 0.52, e suona. Primo aggancio tenuto: **66.1 s**.

Il cancello va costruito **nel motore, prima del guadagno d'analisi**. Misurato
perché non può stare altrove: la banda bassa dopo il make-up legge 0.65-0.70
nell'intro contro 0.73-0.79 con la band — non separa, perché il guadagno
amplifica l'intro 7.5× e a valle «an empty room and a band playing arrive looking
alike - by design» (`updateAnalysisEpoch`). Prima del make-up la differenza
esiste ancora: è lo stesso punto che decide l'epoch, e probabilmente sono lo
stesso lavoro.

Da fare insieme: l'epoch scatta a **53.2 s**, venti secondi dopo l'ingresso della
band (30-40 s), perché il riferimento era già stato trascinato in alto.

### 2. Il caso senza batteria

Il test armonico con pad fallisce ancora, e a 52 BPM la fonte armonica è troppo
lenta. È lo stesso materiale del punto 1 visto da un'altra parte. Se BeatNet non
generalizza serve un modello beat/downbeat addestrato anche su accompagnamenti
tonali: è il pezzo di lavoro più grosso che resta, ed è una decisione, non un fix.

### 3. La verità di fase

Senza una beat-grid annotata sulla registrazione, i 26-33 ms ai livelli bassi, i
61.6 ms a -18 dB e i picchi isolati da 85-167 ms non si confrontano con niente.
Blocca ogni giudizio sul punto 2 della lista dei quattro. Parte dall'utente.

### 4. Quello che resta sui gradini di tempo

- ±2 BPM: rilevati a 3.1 battiti, ma la **fase** rientra dopo. A 120 e 168 il
  rientro è già immediato; a 52 servono ~5 battiti in tutto. Non è una costante
  da stringere: l'errore accumulato cresce col quadrato della durata del battito.
- ±12 BPM: restano lenti (9.3 s a 132, 108 non si stabilizza). È il percorso
  della transizione ordinaria, non il gate dei gradini piccoli.
- Il criterio di PASS di `probe_small_steps` (stabile entro 4 battiti **dal
  cambio**) è sotto il pavimento fisico ai tempi lenti. Va riscritto in
  battiti-dopo-il-rilevamento, o accettato per quello che è.

### 5. Casi noti e non chiusi

- 52 BPM letto 104 a ogni livello: indecidibile su quel materiale (item 1),
  misurato e deliberatamente non asserito.
- 91 BPM a 0 dB: `tryFastAcquire` pubblica 64.55 contro uno state space a 115.38
  con margine **negativo**. Causa nota; la regola `rawBpm < 90` che la provoca è
  la stessa che tiene 76 lontano da 152, quindi serve materiale reale a più
  livelli prima di toccarla.
- L'1 si allinea da solo, ma dopo **dodici battute** (47 battiti,
  `kBeatsToMoveTheBar` sotto `kVoteDecay`). Non è un difetto, è il prezzo scelto;
  è una decisione dell'utente se mezzo minuto è troppo.
- La suite completa non è mai stata eseguita.

## Ipotesi già escluse per misura — non rifarle

1. **Il knob del MIC «sblocca» il tracker.** No: rimescola. La mappa fra
   configurazione ed esito è caotica — un tetto di guadagno di 3× e uno di 24×
   danno lo stesso risultato a sei decimali, 12× ne dà uno dieci volte migliore.
   Quattro corse identiche danno lo stesso numero: è deterministico, non casuale.
2. **Il guadagno d'analisi che oscilla è la causa.** No: a 0 dB e −6 dB la rete
   riceve un segnale identico a tre decimali, e i due agganciano a 66.1 s e 8.5 s.
3. **Un watchdog sulla griglia «affamata»** (accetta meno del 55% dei battiti che
   il suo tempo prevede). Funziona e migliora il sintetico (90→120 da 20.7 a
   10.2 s), ma sul brano vero scambia il crollo a 53 BPM con uno a 131 e arriva
   allo stesso secondo. Codice in scratch.
4. **Adottare il pettine invece di ripartire.** Non scatta, correttamente: nei
   primi 25 s di quel brano il pettine salta 84 → 173 → 87 → 110 → 0.
5. **`cancelPhaseRecovery` reso condizionato** in `beginTempoTransition`: numeri
   identici riga per riga.
6. **La banda bassa come cancello**: non separa dopo il make-up (vedi punto 1).

## I banchi, e cosa ciascuno NON può dire

- `probe_matrix`, `probe_tempo_step`, `probe_small_steps`: pilotano il decoder da
  solo. Non chiamano `setSounding`, quindi **non vedono** la regola che tiene il
  livello sotto una parte che suona: non possono regredire su quella e non
  possono validarla.
- `VPLive`: pilota `BeatTracker`, **non** `VirtualPercussionEngine`. Niente trim,
  canceller, guadagno d'analisi, niente epoch. Su materiale reale dà risposte
  diverse dall'app.
- `VPTrack` (nuovo, `scripts/probe_track.cpp`): motore completo su un file. È
  l'unico banco che misura quello che l'utente sente. Usarlo per tutto ciò che
  riguarda materiale reale.
- `probe_small_steps`: non è end-to-end; il conteggio corretto dopo l'allineamento
  di astra è **7 PASS / 4 FAIL**, non 8/3.
- Nessun banco copre il **timbro** delle voci: solo la regressione d'attacco.

## La regola che è costata di più impararla

In questa sessione **sei ipotesi plausibili sono morte alla misura**, e tre di
esse erano già state scritte nei documenti come cause prima di essere verificate.
Misurare prima di correggere, e rimisurare sul percorso giusto: metà del lavoro
utile di oggi è stato scoprire che stavo misurando mezza catena.
