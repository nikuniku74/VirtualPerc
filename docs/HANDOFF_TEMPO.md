# Ripresa del lavoro sul tempo

Richiesta: implementare progressivamente il piano approvato, con test mirati e
commit locali separati. **Ultima istruzione utente: proseguire SENZA committare**;
questa prevale sul piano iniziale. Non eseguire la suite completa. Nessuna modifica al
submodule JUCE, già sporco all'inizio. Nessuna promessa di perfezione su audio
ambiguo: mantenere il tempo acquisito, oppure attendere/TAP all'avvio.

## Stato

1. COMPLETATO: sicurezza del rientro (annullamento, reset, prove fresche).
2. COMPLETATO: contatori indipendenti dal buffer.
3. COMPLETATO (clock sintetico): recupero dello scarto con fiducia alta.
4. COMPLETATO (integrazione con date accordi note): percorso armonico diretto.
5. INCOMPLETO: detector armonico provato su audio sintetico, due casi falliscono
   l'ingresso (dettagli sotto). Registrazione live ancora da provare.

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
note. Non è stata provata la registrazione live dell'utente né una nuova build
su iPad. Non è stata eseguita la suite completa.

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

Risultati del nuovo gate, che richiede ingresso entro 4.8 s e attacchi entro
25 ms nelle ultime due battute:

- Senza pad: 15 cambi; fase valida per la prima volta a 17.317 s, valida per
  soli 1.845 s complessivi. In quelle finestre tonalShare arriva al massimo a
  0.313, sotto lo 0.55 richiesto dal selettore. Fonte mai selezionata; nessun
  ingresso, nessun attacco.
- Con pad: 38 cambi; fase mai valida, coerenza finale 0.360. Nessun ingresso,
  nessun attacco. Il solo numero di eventi non prova quali siano falsi.
- Errori fase/clock/audio stampati a -1 quando non misurabili: NON zero errore.

La strumentazione è pronta, ma lo step NON è completato e non è stata cambiata
alcuna soglia DSP per far passare il test. Prossima azione: analizzare il gate
di tonalità sul basso/melodia e la stabilità degli eventi col pad, con controlli
negativi per batteria e accordi senza pulsazione prima di modificare i criteri.
Gli otto cambi richiesti restano inoltre incompatibili con due battute quando
c'è un solo accordo per battuta: non basta correggere il selettore di fonte.
Poi usare la registrazione live dell'utente (non identificata in questa sessione).
L'obiettivo entro due battute resta aperto per l'armonia rada: non abbassare il
numero di cambi alla cieca, né attivare il percorso acustico non verificato.
