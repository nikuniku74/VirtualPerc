# Prompt di handoff — debug loop registrati su iPad

Copia integralmente il testo seguente in una nuova conversazione OpenAI aperta
sullo stesso progetto locale.

---

Sto lavorando nel repository `/Users/nicolamarogna/Desktop/NK/VirtualPerc`, una
app C++/JUCE per iPadOS che ascolta musica live e suona percussioni sincronizzate.
Continua il debug direttamente nel codice: non limitarti a proporre un piano e
non modificare il beat tracker salvo evidenza che il problema sia lì.

È stato integrato un banco di 12 loop Dance a 48 kHz sotto
`Assets/Loops/dance`, incorporato nell'app tramite BinaryData. Nelle impostazioni
c'è uno switch LOOP/PATTERN. LOOP usa il renderer ibrido e PATTERN conserva il
vecchio motore a colpi singoli.

Problema osservato su iPad reale: il fallback suona, poi quando dovrebbe entrare
il loop l'audio gracchia e può sparire; successivamente torna in fallback.

Correzioni già applicate e da non annullare:

1. LOOP entra quando `percussionShouldPlay` rende la parte udibile; non aspetta
   più che `TempoRegime` diventi `fixed`. Anche `live` deve continuare a usare il
   loop e seguire accelerando/rallentando.
2. `LoopPlayer::request` non usa più `avoidIndex` a ogni callback. Prima
   alternava le due take quasi a ogni quarto e reinizializzava continuamente gli
   stretcher, causando picchi real-time.
3. Il banco 48 kHz viene rifiutato pulitamente se il dispositivo non è a 48 kHz.
4. Attivando LOOP, la UI imposta automaticamente 48 kHz e buffer almeno 256.
5. Il build iPadOS usa `VP_USE_SIGNALSMITH=OFF`, quindi il WSOLA interno. Il
   primo `Signalsmith::seek` di due stem stereo avveniva nel callback proprio
   all'ingresso del loop ed è la causa più probabile dell'overrun. Signalsmith
   resta disponibile nei build host per confronto.
6. Lo stato UI distingue: serve 48 kHz, swing oltre 18%, BPM fuori banco,
   attesa clock e loop in riproduzione.

Verifiche già eseguite:

- `build-wsola/VPTests_artefacts/Release/VPTests --loops`: 58 test passati,
  0 falliti, backend built-in WSOLA.
- Gli stessi 58 test passano con Signalsmith sul build host.
- Il test verifica esplicitamente che due take non reinizializzino lo stretcher
  a ogni quarto e che una banca 48 kHz su device 44,1 vada in fallback senza
  distorsione.
- `scripts/configure-ios.sh` stampa `Loop time stretch: built-in WSOLA`.
- La build iPad Simulator termina con `BUILD SUCCEEDED`.

Prima azione: chiedimi di installare e provare questa build WSOLA su iPad reale.
Se gracchia ancora, raccogli una sola schermata della pagina debug subito dopo
il guasto e usa soprattutto `callback ms`, buffer effettivo, sample rate,
`riavvii`, analysis gaps/backlog, loopPlaying e stato parte. Poi individua e
correggi la causa nel percorso real-time. Non riattivare Signalsmith nel build
iOS finché il priming non è stato spostato completamente fuori dal callback.

Comandi rilevanti:

```bash
./scripts/configure-ios.sh
./scripts/build-simulator.sh
./build-wsola/VPTests_artefacts/Release/VPTests --loops
```

Rispetta `AGENTS.md`, usa `apply_patch` per gli edit e conserva tutte le altre
modifiche presenti nel worktree.

---
