# Ripresa del lavoro sul tempo

Richiesta: implementare progressivamente il piano approvato, con test mirati e
commit locali separati. Non eseguire la suite completa. Nessuna modifica al
submodule JUCE, già sporco all'inizio. Nessuna promessa di perfezione su audio
ambiguo: mantenere il tempo acquisito, oppure attendere/TAP all'avvio.

## Stato

1. COMPLETATO: sicurezza del rientro (annullamento, reset, prove fresche).
2. COMPLETATO: contatori indipendenti dal buffer.
3. COMPLETATO (clock sintetico): recupero dello scarto con fiducia alta.
4. COMPLETATO (integrazione con date accordi note): percorso armonico diretto.
5. DA FARE: verifica integrata e separazione delle misure sintetiche/reali.

## Regole per riprendere

Leggere `.claude/skills/realtime-tempo/SKILL.md`, questo file e `git status`.
Continuare dal primo step incompleto. Salvare qui comandi, risultati, limiti e
prossima azione dopo ogni step; creare un commit per step verificato includendo
soltanto file pertinenti. I precedenti 8 ms / mezzo beat erano misure del solo
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
