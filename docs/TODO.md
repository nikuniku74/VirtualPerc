# TODO integrazione

Spunta `- [x]` quando un pezzo è integrato e verificato.  
Se scopri qualcosa di nuovo, aggiungi una riga. Se un item cambia, aggiorna il testo, non cancellarlo in silenzio.

**Ordine:** 1 = più importante (senza questo il resto non tiene). Non riordinare per recenza.

**Fuori da questo elenco (standby):** loop registrati (`docs/HANDOFF_LOOP_DEBUG.md`); chiusura formale ciclo Codex PATTERN — sezione **Standby** in fondo. Mic iPad / speaker in stanza: non è il path di verifica (si testa **brano caricato** e **mixer**).

**Come verificare:** per DSP/tempo, un numero di probe o un test; per musica, ascolto. Non dichiarare chiuso un item musicale senza orecchio.

---

## In corso / da fare

### 1. Tempo lento (~50 BPM): cassa / rullo / charleston a ottavi 🟡 (2026-09-04, indagine chiusa: indecidibile dall'audio, mitigato con TAP e ÷2/×2 — resta ascolto)

A tempi lenti (es. **50 BPM**) il groove da batteria è chiaro: **cassa sull'1 e sul 3**, **rullo sul 2 e sul 4**, **charleston (hi-hat) a ottavi**. L'app **non tiene bene il tempo**, anche se il charleston dà un pulse ogni ottavo — non è un brano "vuoto".

Sospetti (skill tempo: sotto ~100 BPM il fold legge gli ottavi come beat; range 40–220; PLL con quarto da 1,2 s): ottava 50 vs 100, fase che deriva, lock che non si chiude. Non "sistemare" bloccando sempre su 100. Il clock non riparte perché il BPM è basso.

**Stato al 2026-09-04 (sessione ChatGPT, crediti finiti a metà — verificato indipendentemente da Claude sullo stesso albero):**

- [x] Riprodotto e diagnosticato: **non è deriva del PLL**. Il decoder resta stabilmente sul livello metrico degli ottavi (legge gli hat come battito, non la cassa). Confermato con probe mirato: 50 BPM mixer → letto ~100 BPM (ottava 0, sbagliato); 50 BPM file → ~100 BPM (sbagliato); 100 BPM mixer/file → corretto in entrambi, come controllo.
- [x] **Prima versione di fix, insufficiente (storico, rimossa il 2026-09-24)** — `BeatDecoder::observeDownbeatCadence()` contava la distanza fra downbeat che superavano una soglia "forte"; se due bar consecutivi misuravano 8 battiti invece di 4, proponeva `metricalOctaveHint` di un'ottava sotto. Il campo diagnostico e l'ingresso di `BeatTracker::updateAutoOctave` restano, ma l'hint è sempre invalido; `kOctaveTooSlow` era stato abbassato 76→49. **Non basta**: il modello reale a 50 BPM non produce downbeat "forti" discreti abbastanza spesso perché il contatore raccolga i due bar consecutivi richiesti — il segnale di battuta del modello è soprattutto un valore continuo su ogni beat, non un evento raro sopra soglia. Verificato di nuovo da Claude il 2026-09-04 con `VPProbe --trace --mixer plain 50`: stesso esito, 50→~100 BPM ancora sbagliato.
- [x] **Corretto (Claude, 2026-09-04) usando la probabilità continua, come indicato** — ma **non basta neanche questo**, e il motivo è musicale, non un bug di implementazione. Riscritto `observeDownbeatCadence()`: istogramma a 8 posizioni sulla confidenza continua di downbeat, indicizzato dal **tempo trascorso** diviso il periodo corrente (non da un contatore di battiti accettati — misurato che la griglia salta occasionalmente un ottavo anche su segnale pulito, e un contatore a incrementi si sfasa per sempre dopo ogni salto; l'indicizzazione a tempo assorbe il salto, come già fa il fit del tempo altrove nel file). Test: bin dominante vs bin opposto (4 posizioni via) debole = livello sbagliato.
  - **Causa per cui non funziona su questo materiale:** misurato con `VPProbe` che la curva di downbeat della rete attiva **comparabilmente sia sul vero battere 1 che sul vero battere 3** — la stessa identica ambiguità 1-vs-3 già annotata nell'item 2 ("1 vs 3 resta l'ambiguità"). A ottava sbagliata, battere 1 e battere 3 cadono esattamente a 4 posizioni di distanza nell'istogramma a 8: la stessa identica "firma" che il test usa per dire "livello già corretto". Il segnale di downbeat da solo **non può distinguere** le due ipotesi su questo materiale.
  - **Alternativa provata e falsificata:** l'ampiezza generale di attivazione (beat, non downbeat) non alterna debole/forte fra ottavi on-beat e off-beat come ci si aspetterebbe (misurato: resta alta 0.5–0.98 su quasi ogni ottavo) — la rete tratta gli ottavi dell'hi-hat come impulsi salienti quanto cassa/rullo, quindi neanche l'ampiezza aiuta.
  - **Pista armonia (item 2, `HarmonicChange`/`barFromHarmony`) scartata**: il materiale del bug è **batteria pura** (cassa/rullo/hi-hat, nessun accordo) — non c'è alcun contenuto armonico da cui `HarmonicChange` possa estrarre un cambio. Questa pista non si applica a questo bug specifico.
  - **Conclusione onesta:** con i soli tre scalari che il decoder riceve dalla rete (pBeat, pDownbeat, pNone) non ho trovato un discriminante statistico affidabile per questo materiale. Risolverlo per davvero richiederebbe probabilmente o (a) una nuova feature con accesso al contenuto spettrale grezzo (bassa frequenza di cassa/rullo vs hi-hat, che il decoder oggi non riceve), o (b) la stessa scala di validazione empirica (molti brani/stili) già usata per tarare il resto di questo file — non un correttore isolato. Non riprendere questa via senza uno di questi due investimenti.
  - Stato storico: questa indicizzazione era rimasta nel tree per misurare il tentativo. Il 2026-09-24 il percorso disattivato è stato rimosso; la diagnosi e i probe restano documentati qui e in `docs/HANDOFF_OCTAVE_50BPM.md`.
- [x] **Feature spettrale provata (Claude, 2026-09-04) — e qui l'item cambia natura: il caso è indecidibile dall'audio.** Piano, misure e numeri in `**docs/HANDOFF_OCTAVE_50BPM.md`** (leggerlo prima di riprovare qualsiasi cosa qui).
  - Portata fino al decoder l'energia delle bande basse del frame che già alimenta la rete (`LogSpectFeatures::lowBandEnergy`, 24 bande, ~30–250 Hz: cassa e rullo hanno corpo lì, l'hi-hat no). Test: profondità dell'alternanza fra le due classi di battiti accettati.
  - **Sul caso target funziona:** 50 BPM passa da 100.02 a **50.03**, e 76/100/118/132/140 + stili syncopated/pad/sync+pad restano invariati. Divario misurato netto: profondità 0.43 (lettura sbagliata) contro 0.85 (lettura giusta).
  - **Ma rompe il half-time:** materiale half-time a 100 BPM (rullo solo sul 3) passa da 99.92 a 60.13 instabile. A/B fatto: è il correttore. E **non è una soglia da spostare**: in half-time le posizioni 2 e 4 sono vuote in banda bassa esattamente come gli ottavi dell'hi-hat a 50 — *stessa spaziatura, stesso basso sotto gli stessi slot*. Una battuta straight a 50 e una half-time a 100 **sono lo stesso suono**; quale dei due si chiami "il tempo" è una convenzione, non un dato acustico.
  - **Correttore respinto e rimosso il 2026-09-24**: era spento con `kCadenceCorrectionEnabled = false`, ma continuava ad aggiornare l'istogramma a ogni battito. Restano il banco e i numeri in questo documento e in `docs/HANDOFF_OCTAVE_50BPM.md`; la feature spettrale serve ancora ai controlli attivi cassa/charleston.
- [x] **Conseguenza, decisa dall'utente il 2026-09-04: rimessi i ÷2/×2.** Se l'audio non può decidere, serve qualcosa da fuori: TAP c'era già, e ora ci sono di nuovo anche i due pulsanti (item 15, ripristinati e testati end-to-end). **Questo non "chiude" l'item 1**: la lettura automatica a 50 BPM resta sbagliata: quello che cambia è che ora il musicista ha come rimediare in un gesto. I quattro test RED dell'altra sessione (`mixer/file slow kit holds 50 BPM…`) restano rossi apposta, e vanno letti così: descrivono un obiettivo che sappiamo non raggiungibile per via automatica.
- [x] Verifica fatta con strumenti mirati (`VPProbe --trace --mixer <stile> <bpm>`, pochi secondi l'uno) su tutta la matrice 50/76/100/118/132/140 × straight/syncopated/pad/sync+pad/half-time, non con la suite intera. Vedi item 16 e la nota in fondo al file.
- [x] Docs in `.claude/skills/realtime-tempo/SKILL.md` — scritto: §2 "For one class of material it is not *partly* unsolvable - it is undecidable", con le tre vie tentate, i numeri e la tabella che mostra perché straight-a-50 e half-time-a-100 sono lo stesso segnale. Sostituisce la vecchia riga che dava per scontato che i casi ambigui si risolvessero nel tracker.
- [x] Gate finale `VPTests` intera: lanciata su richiesta esplicita dell'utente (vedi esito in fondo all'item).
- [ ] **Ascolto** — nessun orecchio umano su questo item finora. È l'unica verifica rimasta che non posso fare io: serve sentire un brano lento reale (mixer o file caricato) e dire se la lettura a ottava doppia è davvero il problema che si sente, o se con il TAP il caso è già coperto in pratica.

---

### 2. Capire qual è il primo quarto (e riallinearlo dopo un taglio) ✅ (2026-09-07)

L'app deve mettere l'**1** sul primo quarto del 4/4 su mixer e brano caricato, senza «L'1 è QUI». Se il musicista **taglia due quarti** e riprende in 4/4, deve riallineare l'1 in poche battute. Niente look-ahead; il clock del tempo non riparte; ruotare l'1 = solo `rotateBarIndex`.

Chiuso: istogramma downbeat + armonia (`BeatTracker::alignBarFromVotes`). In play servono **47 battiti accettati** (~12 battute a 4/4, cioè 23,6 s a 120 BPM e 47,2 s a 60) prima di ruotare — vedi la tabella nell'item 22; qui c'era scritto «~8 battute», che era sbagliato. Un buco di due quarti apre una finestra coming-in di 4 battute (`notifyBarReentry`, 8 beat di evidenza, una rotazione); il decoder **non** riparte. `barLocked` **vieta** ogni rotazione automatica. Sul percorso speaker c'era un gate circolare: l'allineamento veniva chiamato solo se `barFromHarmony` era già vero, ma quel flag può diventare vero soltanto dentro l'allineamento. Corretto chiamando sempre l'allineamento, saltando invece il solo istogramma neurale (misurato a caso sul ritorno acustico) e lasciando rispondere l'armonia. Su materiale che non contiene alcun indizio affidabile di battuta l'1 non è deducibile: in quel caso resta intenzionalmente `SPOSTA L'1`, non una rotazione casuale.

- [x] **Baseline:** il banco precedente ha misurato ~20–21/25 sul line feed e voto vicino al caso sul ritorno acustico; la segnalazione d'ascolto ha esposto il percorso armonico irraggiungibile, non una soglia da allentare.
- [x] **Test RED taglio:** `VPTests --bar` — following, tace 2 quarti, downbeat del modello su quello che il clock chiama 3. Entro 4 battute `beatInBar` è 0; decoder `analysisRestarts` invariato. Fill senza buco di livello non ruota. Con lucchetto non ruota. Seek: stessa finestra, niente epoch.
- [x] **Finestra di rientro:** `maybeDetectBarReentry` (picco del blocco vs `levelLoud`, non l'epoch da 4 s) + `notifyTrackSeek` → `BeatTracker::notifyBarReentry`. Senza `notifyInputRestart`. Una rotazione. RT: niente alloc/lock/I/O.
- [x] **Ingressi:** file/mixer continuano a usare prima il voto neurale; speaker esclude quel voto e può ora entrare nell'armonia. Il test end-to-end armonico usa esplicitamente `FollowSource::speaker`, così il gate circolare non può tornare.
- [x] **Docs:** `.claude/skills/realtime-tempo/SKILL.md` + `docs/AUDIO_ENGINE.md` (rientro; lucchetto; clap `barTrusted`).
- [x] **Gate rapido:** `VPTests --bar` — 10/10 PASS (taglio, fill, lucchetto, seek). Suite completa non lanciata, come richiesto. Il clap legge `tr.barTrusted`; una prova percettiva su iPad resta uno smoke test utile, non lavoro software aperto.

Piano discusso: Task 0 = baseline; 1 = RED; 2 = finestra rientro; 3 = path file; 4 = docs.

---

### 3. Cambio brano a START già on (60 → 120 non riallinea) ✅ (2026-09-14)

Se carico un brano a es. **60 BPM**, premo **START**, poi carico **un altro** a **120 BPM**, l'app **non si allinea subito**. Se prima premo **STOP**, al nuovo brano **si allinea subito**.

Oggi `loadInternalTrack` sostituisce il file e fa `trackTransport.start()`, ma **non** resetta il tracker (`engine.stop()` / `notifyInputRestart`). Resta il lock del 60 sul pezzo a 120.

Un file nuovo è un **ingresso nuovo**, non una deriva del brano precedente. L'utente non deve dover premere STOP. START può restare acceso; BPM e fase devono chiudersi sul B con la stessa prontezza di uno START pulito. Il clock non "riparte" a caso a ogni BPM: è il cambio di sorgente che va dichiarato.

**Fatto (2026-09-14): nuovo ingresso dichiarato al load.**
`VirtualPercussionEngine::notifyInputRestart()` (`VirtualPercussionEngine.h`/`.cpp`)
alza un `inputRestartPending` atomico; `process` lo consuma prima di
`setSourceAudible`, azzera lo stato di livello/ritmo dell'ingresso precedente
(`resetAnalysisLevelState`) e **incrementa** `analysisEpoch` con
`preserveCombOnEpoch = false`, cioè lo stesso evento di un cambio sorgente
misurato in item 19/29. `loadInternalTrack` lo chiama dopo `selectFollowSource` +
`trackTransport.start()`. L'orologio non riparte: cambia solo l'evidenza su cui
il decoder era agganciato. `notifyTrackSeek` resta invariato (un seek non è un
brano nuovo). `resetAnalysisLevelState` non azzera più l'epoch da sé: lo fanno
`prepare()` e `reset()` esplicitamente, così l'epoch può solo avanzare.

- [x] Riprodurre: START su file A (60) → CARICA file B (120), senza STOP. Oggi: tempo/fase del A. Con STOP in mezzo: lock rapido sul 120.
- [x] Al load (e analogo seek, item 14): nuovo ingresso (restart decoder / finestra coming-in), non continuare il PLL del A. RT: niente alloc.
- [x] Percussioni sul B in poche battute, non restare a 60 per 8–20 s.
- [x] Test: `VPTests --new-input` (anche dentro `vpRunAiBeatTests`). Stub a impulsi, decoder vero: A 60 → B 120 + `notifyInputRestart()`. **`bpm 60.0 -> 120.0`, `restarts 0 -> 1`**, 3/3 PASS. Controprova con la consumazione disattivata: **`bpm 60.0 -> 60.2`, `restarts 0 -> 0`**, 2 FAIL — la correzione è quella che riaggancia. Gate: `--bar` 10/0, `--swing` 3/0, `--evidence` 2/0, `--tempo-slow` 10/0, `--state-timing` 3/3, `--makeup e` 4/0, `--makeup f` 7/0.
- [ ] **Ascolto** su iPad/Mac: caricare un secondo brano senza STOP e sentire che entra sul B in una o due battute.
- [x] Docs skill tempo: cambio file ≠ taglio in-song (item 2).

---

### 4. Drift guard (muto se è esageratamente fuori tempo)

Se la parte è **esageratamente fuori tempo** rispetto al brano, **non deve suonare nulla** e deve **riprendere appena rientra** sul tempo. È una **guardia**, non un riallineamento: non sposta l'1 e non cambia il BPM; tace e riattacca.

**UI (impostazioni):** toggle **Drift guard**, **acceso di default**, disattivabile. Sotto, una riga di descrizione, es.:

> Se il tempo stimato è troppo lontano dal brano, le percussioni tacciono e ripartono da sole quando il lock è di nuovo solido.

Soglia "esageratamente" da misurare (fase, confidenza, stato `following` vs `lowConfidence` / `recovering`). Non confondere con IN ASCOLTO / `BandDynamics` (quello è il volume della band, non lo scarto di fase). A 50 BPM (item 1) la guardia non deve mutare un lock lento-ma-giusto.

- [ ] Definire il criterio (fase oltre X ms e/o lock perso) e l'isteresi del rientro, così non batte on/off.
- [ ] Setting `driftGuard` (o nome affine) default **on**; audio thread legge solo atomic.
- [ ] Mute della parte (shaker, congas, clap, cembalo) senza fermare tracker/clock; phrase count continua a correre (come già per il mute).
- [ ] UI: toggle + descrizione breve in impostazioni.
- [ ] Test: fuori tempo → silenzio; rientro → suona; toggle off → continua a suonare anche storto.
- [ ] Ascolto: un dropout breve, non un buco di battute dopo che è già riallineato.

---

### 5. Cambio parte (style) → esce dal tempo ✅ (2026-09-07, non riprodotto)

Segnalazione: cambiando lo stile delle percussioni sembrava che la parte perdesse il tempo. Il cambio è stato isolato dal normale movimento della rete con `VPOps --style-change`: non muove il clock. Sul brano caricato il delta contro la corsa di controllo è **−0,00 ms**; sul percorso iPad **+1,93 ms**, sotto la risoluzione di circa 10 ms del banco. Sul mixer le corse indipendenti della rete non sono deterministiche (una seconda corsa di controllo è uscita da sola fino a 254 ms), quindi non si attribuisce quella deriva al pulsante. Il nuovo stile cambia accenti e buchi del pattern e può dare una diversa impressione metrica senza che la griglia si sia spostata.

- [x] Riproduzione mirata: MARCHA → ROCK, 118 BPM, START già on, brano diretto / iPad / mixer.
- [x] Traccia: `setGrooveStyle` scrive solo `requestedStyle`; `applyPendingGrooveControls` committa sul successivo quarto. Non chiama tracker, clock, `alignPhrase`, reset o re-anchor.
- [x] Verifica rapida: `VPOps --style-change [--file|--mixer] --bpm 118`. Brano −0,00 ms; iPad +1,93 ms; nessun gap/restart attribuibile al cambio. Il test unitario esistente verifica inoltre che stile e swing aspettino il quarto successivo.
- [x] Esito: nessun fix al motore necessario. Se ricompare all'ascolto, annotare stile di partenza/arrivo e sorgente: va confrontato il pattern percepito o la deriva del materiale, non riaperto il clock senza una misura sopra il rumore del banco.

---

### 6. Pulsante AUTO (STRUMENTI) — densità che segue l'ottava del tempo

In **STRUMENTI**, **AUTO** oggi è solo un alias di **1/8**: non ascolta il brano e non cambia mai griglia. Deve diventare la modalità che fa quello chiesto sul raddoppio/dimezzamento.

**Manuale (fisso):** `1/4`, `1/8`, `1/16` restano quello che premi. Non si auto-adattano. Se il BPM raddoppia con 1/16 acceso, la parte resta a sedicesimi (e suona più veloce): è una scelta esplicita.

**AUTO:** tiene la **densità percepita** quando il tempo **raddoppia o dimezza** (ottava metrica). Copre tutte e tre le griglie, spostandosi tra loro. Il clock resta a sedicesimi; si cambia solo il thinning in `GrooveEngine`. Niente restart del clock, niente look-ahead.

Tabella (ottava 0 = lock "giusto", densità di riposo = **ottavi**, come oggi):


| Ottava del tempo          | Griglia che suona |
| ------------------------- | ----------------- |
| −1 (BPM circa **metà**)   | **1/16**          |
| 0                         | **1/8**           |
| +1 (BPM circa **doppio**) | **1/4**           |


Così: se AUTO era sugli ottavi e il tempo raddoppia, non resta a 1/8 sul BPM doppio (troppo fitto) → passa a **quarti**. Se dimezza → **sedicesimi**, così non resta vuota. Stessa logica in tutte le direzioni.

Fuori dai salti ×2 / ÷2 (un +5 % di BPM, un fill) **non** deve cambiare griglia.

UI: AUTO acceso = questa logica. `1/4` / `1/8` / `1/16` spengono AUTO. Non confondere con **AUTO in PARTE** (scelta dello stile). Eventuale riga in impostazioni, es.: *«AUTO regola quarti / ottavi / sedicesimi se il tempo raddoppia o dimezza.»*

- [ ] Riprodurre raddoppio/dimezzamento (octave auto vs lock sbagliato) e distinguere i due casi.
- [ ] Implementare **solo** con `subdivision == autoDetect`; mappare ottava → 1/4, 1/8 o 1/16 nel thinning, senza scrivere sopra la scelta AUTO nei settings.
- [ ] `1/4` / `1/8` / `1/16`: comportamento attuale, invariato.
- [ ] Test: AUTO a ottava 0/±1 dà la tabella; manuale 1/16 a ottava +1 **resta** 1/16; niente switch su fill o deriva piccola.
- [ ] Ascolto: AUTO + tempo che raddoppia non deve suonare "doppio veloce"; dimezzando non deve restare troppo rada.
- [ ] Docs skill percussioni: AUTO non è più "significa ottavi fissi".

---

### 7. Swing: pulsante ON/OFF in STRUMENTI (niente knob) 🟡 (2026-09-07, forma corretta sul riferimento dell'utente + valore scelto d'orecchio — resta ascolto in app)

Oggi lo swing è una **knob** 0–100% (`swingSlider` → `settings().swing` 0..1). Non serve una quantità: o è **dritto** o è **swing**. Togliere la knob; in **STRUMENTI** (accanto a shaker / congas / AUTO / 1/4 / 1/8 / 1/16) un pulsante **SWING** acceso/spento.

**ON** = swing pieno (terzina): l'"&" sta a **due terzi** del quarto (`kFullSwingBeats = 1/6`, `humanDelay` in `GrooveEngine`). **OFF** = 0, griglia dritta. Nessun valore intermedio in UI. Cambio come già per parte/swing: **al prossimo quarto**, non a metà beat.

- [x] UI: pulsante **SWING** in STRUMENTI (decimo quadrato, accanto a NATURALE, stesso `setupBtn` / stesso acceso-fucsia). Knob `swingSlider` + label + valore **via**, FEEL passa da 8 a 7 manopole, `hInst` da `squareFor (9)` a `squareFor (10)`. Prefs: scritte 0/1, ma **lette dal vecchio double**, così un'installazione che aveva la knob a metà torna su "acceso" invece che su un valore che la UI non sa più mostrare (`> 0.5` = swing).
- [x] DSP: `applySwing` scrive `swing = 1` / `0`. **La formula del warp non è stata toccata: era già giusta.** Verificato prima di mettere mano alla UI.
- [x] **Swing fatto per davvero — misurato, non "acceso".** Probe usa e getta su `GrooveEngine::eventsAt` con humanize a 0, tutti e 16 gli step, marcha 1/16:

  | step | dritto | swing pieno | atteso |
  |---|---|---|---|
  | 0/4/8/12 (quarto) | 0,0000 | **0,0000** | 0 |
  | 1/5/9/13 ("e") | 0,2500 | **0,3333** | 1/3 |
  | 2/6/10/14 ("&") | 0,5000 | **0,6667** | 2/3 |
  | 3/7/11/15 ("a") | 0,7500 | **0,8333** | 5/6 |

  16 step su 16 esatti, in entrambi gli stati. `delayBeats ≥ 0` verificato anche a swing 0,25 / 0,5 / 0,75 (mai anticipo). Humanize resta un termine a parte: il probe lo azzera e i numeri sopra sono solo lo swing. Commit al prossimo quarto: test già esistente in `TestMain`, invariato.
- [x] Il probe è stato buttato e l'asserzione vive nella suite: **`swing-grid`** in `Tests/TestMain.cpp`, tre `expect` (dritto dov'è scritto / terzina / mai anticipo). Ha un filtro suo, **`VPTests --swing`**, come `--leak` e per lo stesso motivo: sedici chiamate a `GrooveEngine` non devono stare dietro minuti di worker neurale. Girato: `3 passed, 0 failed`.
- [x] Render per l'ascolto: `VPRender --style marcha --bpm 112 --sub 16 --click --swing 0|1`, mandati all'utente.
- [x] Loop registrati: il path era già corretto e non silenzia — `HybridPercussionRenderer` rende **sempre** il motore a colpi come fallback, quindi con SWING acceso il banco rifiuta (oltre `LoopBank::swingTolerance` 0,18) e si resta sul sintetico. Cambiata solo la scritta in SETUP, che diceva `LOOP (swing massimo 18%)` e si poteva leggere come "il loop sta suonando con al massimo il 18%": ora `PATTERN (SWING: il banco non ha prese swingate)`.
- [x] Docs skill percussioni: §8 dice tasto, non knob, con la tabella della geometria e il rimando a `swing-grid`.
- [x] **Lo swing era della forma sbagliata, e il riferimento dell'utente lo ha dimostrato (2026-09-07).** L'utente ha portato un riferimento — shaker Afrobeats, 106 BPM, `53_SAM4_SHAKER_106BPM - Bbm.wav` — dicendo «l'andamento è questo, è una specie di sincope». Misurato (onset a 1 ms, 24 colpi per posizione, dispersione dei cluster 1–8 ms):

  | modello | errore medio |
  |---|---|
  | dritto | 20,8 ms |
  | **warp del quarto** (quello che c'era: l'"&" a 2/3) | **26,6 ms** — peggio del dritto |
  | **warp dell'ottavo** (0, 1/3, **1/2**, 5/6) | **7,6 ms** ✓ |

  Gli **ottavi restano fermi** (0,516 e 0,484 di quarto, 9 ms dall'even): a muoversi è solo il sedicesimo *dentro* l'ottavo, che cade al 57,4 % e 65,7 % (media **61,6 %**; dritto 50 %, terzina piena 66,7 %). Più un accento 2× su un colpo per quarto. Warpare il quarto fittava quella musica **peggio di non swingare affatto**, perché l'unico colpo che sposta è quello che la musica tiene fermo.
- [x] **Fix: lo swing warpa la griglia che si sta suonando**, non sempre il quarto. `GrooveEngine::humanDelay` + `swungWithinSpan`: span = il quarto su una parte a 1/8, l'ottavo su una parte a 1/16. Stesso warp, un livello sotto. A 1/16 e swing pieno i sedicesimi cadono su 0, 1/3, **1/2**, 5/6 invece di 0, 1/3, 2/3, 5/6. Il comportamento a 1/8 è **identico a prima**.
- [x] Test riscritto: `VPTests --swing` ora copre quattro casi (1/16 e 1/8, dritto e swing) e ha un'asserzione dedicata — **su una parte a 1/16 l'"&" non si sposta di un campione**, che è tutta la differenza fra shuffle e sincope. `3 passed, 0 failed`.
- [x] Rese verificate col misuratore, non a occhio: `--sub 16 --swing 1` mette l'ottavo a **0,5002** di quarto (dritto) e fitta il modello a sedicesimi con 3,1 ms d'errore.
- [x] **«Acceso» vale 0,65, scelto d'orecchio sui tre render — non la terzina piena.** `kSwingOnAmount` in `MainComponent::applySwing`. Posizione scritta 60,8 %, resa misurata **63,0 %** contro il **61,6 %** del riferimento: 1,4 punti, dentro gli 8 punti di dispersione del riferimento stesso (57,4 vs 65,7 fra i suoi due ottavi). La terzina piena rende 68,9 %, sopra tutto l'intervallo del riferimento. Inseguire il 61,6 % al decimale sarebbe falsa precisione: le due metà della battuta del riferimento non sono d'accordo fra loro.
- [ ] **Ascolto** del tasto in sé: se ON/OFF è la scelta giusta e se a 1/8 lo shuffle suona ancora come deve.

---

### 8. STOP: uscita a trillo dello shaker (poi fade)

Oggi STOP è **immediato**: `engine.stop()` → tracker off, `percussion.silence()`, loop cut (`LoopPlayer`: *no fade, no bar line*). Troppo secco.

Quando si preme **STOP**, lo **shaker** fa la **classica uscita**: un trillo, suono **velocissimo** (colpi molto più fitti della griglia 1/16), **lungo**, con un **fadeout** che lo accompagna fino al silenzio. Non un click che sparisce; è il gesto da percussionista a fine brano.

Solo lo shaker. Congas / clap / cembalo si fermano subito (o in pochi ms, niente click). Il tracker può già essere stopped: il trillo **non** segue più il BPM del brano, è una coda autonoma. Niente alloc/lock/I/O in audio thread.

Durata e densità da fissare all'ascolto (ordine di grandezza: circa 1–2 s di trillo che cala). Se si ripreme START a metà coda, la coda si taglia e si rientra puliti.

- [ ] Stato "outro" in `PercussionEngine` (o equivalente): STOP arma la coda, non `silence()` nello stesso sample.
- [ ] Trillo shaker (down/up rapidissimi) + gain che scende fino a zero; poi silenzio vero.
- [ ] Altre voci: cut immediato, niente trillo.
- [ ] START / TAP a coda aperta: abort della coda, niente doppio attacco.
- [ ] Test: STOP non è più un mute al sample 0; la coda finisce; START durante la coda riparte. Ascolto obbligatorio.
- [ ] Docs skill percussioni: STOP ≠ cut, è l'uscita a trillo.

---

### 9. Volumi separati shaker e congas ✅ (2026-09-03)

Oggi c'è **una knob** per le percussioni. Servono **due volumi** (shaker / congas), indipendenti.

`EngineSettings::instrumentMix` (balance 0..1) e `percussionVolume` sono stati
sostituiti da `shakerVolume` / `congaVolume` (`atomic<float>`, default **1.0**
ciascuno — stesso suono di prima, che aveva percussionVolume fisso a 1.0 e
mix a 0.5). `PercussionEngine::setShakerVolume` / `setCongaVolume` scrivono i
due gain per voce in `render()`, sostituendo la curva di balance. Stesso
schema in `HybridPercussionRenderer::Input` per il path loop registrati
(`shakerVolume` / `congaVolume` invece di `instrumentMix`), così i volumi
valgono anche quando suona la registrazione, non solo il motore sintetico.

UI: la knob unica SHAKER↔CONGAS in FEEL è diventata due manopole indipendenti
("SHAKER", "CONGAS", 0–100%, default 100%). Prefs: chiavi `shakerVolume` /
`congaVolume` al posto di `instrumentMix`.

- [x] Settings + UI (due controlli; default musicali: entrambi al 100%, invariato rispetto a prima).
- [x] DSP: guadagni per voce in render, audio thread safe (solo store/load atomici, nessun alloc/lock).
- [x] Test: uno a zero silenzia solo quella famiglia; l'altro resta (`Tests/TestAiBeat.cpp`, blocco "voice-volume").
- [x] CLAP e CEMBALO (item 10) avranno ciascuno un volume proprio: non unire tutto in una knob — soddisfatto per costruzione, `shakerVolume`/`congaVolume` sono due `atomic<float>` indipendenti in `EngineSettings`, non una curva di balance; item 10 aggiungerà `clapVolume`/`cembaloVolume` con lo stesso schema, non toccherà questi due.

---

### 10. CLAP e CEMBALO oltre shaker e congas ✅ (2026-09-07)

Due voci in più, ognuna con **enable** e **volume proprio** (vedi item 9). Stesso clock a 16th.

**CEMBALO** = **tamburello** (2026-09-03: chiarito dall'utente — non un piatto). Suona con le prese VCSL *Tambourine*: colpo sul pulse, shake sul ritorno. Non è un pattern a parte: è **lo stesso mestiere dello shaker**, con un altro suono. Stessa griglia, stesso thinning (AUTO / 1/4 / 1/8 / 1/16, item 6), stesso swing, stessi accenti/pesi per stile, stesso humanize/dinamica. Acceso, suona sugli stessi step dello shaker; spento, tace. Shaker e cembalo possono stare entrambi on (due timbri sulla stessa parte) o uno solo. L'invariante "niente conga sul uno" **non** riguarda il cembalo: come lo shaker, può (e deve) cadere sul pulse.

**CLAP.** Non segue lo shaker. Suona **solo sul rullo**: in 4/4 i quarti **2 e 4** (backbeat), non ogni step.

**Allineamento (obbligatorio, item 2):** quei quarti devono essere l'1/2/3/4 **di quello che l'app sta sentendo**, non del conto interno se è ruotato. Se `beatInBar` è sfasato di due quarti, il clap "sull'1 e sul 3" dell'app in sala sta sul **2 e sul 4** (e il clap "sul rullo" sta sul 1 e sul 3). Non accettabile. Il clap **non parte** finché la battuta non è fidata (stesso criterio dell'item 2: 1 tenuto, non 1 vs 3 ambiguo, riallineato dopo un taglio di due quarti). Non "sistemare" il pattern a orecchio (mettere 2 e 4 per compensare un 1 sbagliato). Con lucchetto acceso vale il conto dell'utente.

Fatto: due nuovi `Stroke` (`clap`, `cembaloDown`/`cembaloUp`), sintesi fallback dedicata (`PercussionEngine::synthesizeCymbal` per il cembalo, un caso in `specFor` per il clap — nessun sample in `Assets/Percussion/`, non richiesto dall'item). `GrooveEngine::eventsAt` genera il cembalo riusando `spec.shaker[step]`/thinning/accento identici allo shaker (blocco separato, switch proprio); il clap è un pattern fisso (step 4/12) fuori da ogni tabella di stile, gated da `setBarTrusted`. `EngineSettings::clapEnabled/cembaloEnabled/clapVolume/cembaloVolume`, UI in STRUMENTI (tasti CEMBALO/CLAP) e FEEL (manopole), entrambi **off di default**. `kMaxEvents` 4→6 per il posto in più sulla stessa sedicesima.

**Allineamento chiuso con item 2:** il gate del clap legge `BeatTracker::barTrusted` (lucchetto, oppure istogramma sullo zero fuori dalla finestra di rientro). Il proxy a due battute da `barRotations` è stato sostituito; taglio e seek sono coperti da `VPTests --bar`.

**Buco noto (loop registrati, standby):** clap e cembalo esistono solo nel motore sintetico — nessuno stem registrato. Con `VP_ENABLE_RECORDED_LOOPS` on (default) e una registrazione che prende il sopravvento, le due voci si spengono insieme al resto dei "single strokes" (vedi `HybridPercussionRenderer::Input`, commento vicino a `shakerVolume`). Fuori scope qui, è lavoro Standby B.

- [x] Articolazioni in banca (o sintesi fallback) + `Stroke` per clap e per cembalo — **sample registrati VCSL** (CC0): clap dalle prese d'insieme *Claps*, cembalo (= tamburello) da *Tambourine 1/2*. 13/13 articolazioni suonano da registrazione, nessuna sintesi (la sintesi resta solo come fallback senza asset). Scelte per attacco misurato, non a occhio: vedi `Assets/Percussion/ATTRIBUTION.md`.
- [x] Enable + volume per ciascuno.
- [x] Cembalo: riusa la logica/eventi shaker (stessi step, stessa suddivisione); solo sample/voce diversi.
- [x] Clap: solo step del rullo (default 2 e 4); silenzio altrove.
- [x] Clap gated sulla battuta fidata — `tr.barTrusted` dal tracker (item 2).
- [x] Test: `Tests/TestAiBeat.cpp` — cembalo segue 1/4 vs 1/16 come lo shaker (bit-identico allo shaker sui tre subdivision); clap solo su step 4/12 in ogni stile, muto se non fidato; volumi indipendenti (blocco "clap-cembalo" dopo "voice-volume", stesso schema dell'item 9). "non del bar index storto": non testabile fino a item 2 (non c'è ancora un caso reale di rotazione da riprodurre).
- [x] Ascolto render sintetico (2026-09-04): `VPRender --style rock --bpm 120 --bars 8 --click --no-shaker --no-congas --clap --cembalo` → `/tmp/clap-cembalo.wav`. Orecchio: clap sul 2 e 4 del click, cembalo da tamburello sulla griglia shaker. Il clock qui è forzato, quindi **non** copre l'allineamento al brano.
- [x] Ascolto in-app su brano caricato (2026-09-04): clap coincidente col rullo vero, cembalo al posto giusto. Mixer non provato in questa sessione.
- [x] Dopo un taglio di due quarti tace durante la finestra non fidata e torna sul rullo dopo il riallineamento (`VPTests --bar`, item 2).
- [x] Docs in `.claude/skills/percussion-patterns/SKILL.md` (nuovo §7, tabella stroke aggiornata).

---

### 11. Shaker più naturale (non griglia fissa) 🟡 (2026-09-04, codice + test ok — resta ascolto)

Oltre a 1/4, 1/8, 1/16 fissi: in **ottavi**, ogni tanto qualche **sedicesimo** (e analoghi sulle altre griglie, se ha senso). Thinning che **aggiunge** eccezioni, non solo toglie.

- [x] Spec musicale: probabilità, su quali step (e/a), mai sul 1 delle congas; quanto spesso a 1/8 vs 1/4.
  - 1/8: step dispari (e/a), chance `kNaturalEighthChance = 0.22` × `(0.4 + 0.6 * intensity) * dynamics`. Usa i valori già scritti in `spec.shaker[16]`.
  - 1/4: solo gli off-eighth (step 2/6/10/14), chance `0.30` con la stessa scala. Non salta di due griglie (niente 16th su un 1/4).
  - 1/16: niente da aggiungere, invariato.
  - Congas intatte, incluso il guard `step != 0`.
- [x] Implementare in `GrooveEngine` (RNG deterministico, seed come il resto). `soundingShaker` è una decisione sola, condivisa da shaker e cembalo.
- [x] Opzione dedicata (non rubare il tasto 1/8). **NATURALE** in STRUMENTI, default **off**, prefs `shakerNatural`.
- [x] Test: 1/8 puro vs naturale (ci sono odd stroke; non diventa un 1/16 pieno). Stesso per 1/4; cembalo bit-identico sugli extra; default off. In `Tests/TestAiBeat.cpp` dopo il thinning shaker.
- [ ] Ascolto.

**Review (Claude, 2026-09-04)** — comportamento verificato indipendentemente con
una probe che linka solo `GrooveEngine.cpp` (niente suite): 1/8 → 55 ornamenti su
32 battute (limite del test 128), 1/4 → 41 off-eighth e **0** sedicesimi, 1/16 →
stream identico bit a bit, golden 1/8 pre-feature identico, dinamiche 1.0/0.5/0.2
→ 55/35/8 ornamenti. Shaker e cembalo accesi **insieme**: 325 step coincidenti,
0 disallineati. Peggior caso di eventi su una sedicesima, tutte le voci e tutti
gli stili: 4 di `kMaxEvents` 6. Due buchi nei test, il codice è giusto ma il test
non lo dimostra:

- [x] «cembalo NATURALE lands on the same extra steps as the shaker» confrontava
  ```
  due run separate con l'altra voce spenta, e solo i conteggi — la regressione
  che teme (due rollate → desync) si vede solo con entrambe accese.
  **Corretto:** aggiunto `"shaker and cembalo share one NATURALE roll when
  both are on"`, che gira con shaker *e* cembalo accesi e confronta il conteggio
  per singolo step, contando gli ornamenti perché non possa passare a vuoto
  (misurato: 69 ornamenti, 0 step disallineati).
  ```
- [x] «NATURALE never puts a conga on the first quarter's down-stroke» era vacuo
  ```
  due volte: shaker e cembalo erano off (quindi `shakerOn || cembaloOn` false e
  `soundingShaker` mai chiamato) e il loop chiedeva solo lo step 0, che NATURALE
  non tocca mai. **Corretto:** shaker acceso, tutti e 16 gli step percorsi, e
  `naturalOrnaments > 0` nell'assert perché non possa tornare vacuo
  (misurato: 51 ornamenti).
  ```
- [ ] Nota misurata, non un bug: con NATURALE on lo stream RNG condiviso si
  ```
  sposta, quindi le congas suonano **le stesse note** ma con velocity/delay
  humanize diversi (406 eventi su 408 a humanize 0.35). Nessuna nota cambia
  perché ghost e step dispari sono comunque scartati su 1/8 e 1/4. È
  l'invariante che il discard-buffer esiste apposta per proteggere, e non
  c'è un test che la fissi in questa direzione.
  ```

**Igiene dell'albero (Claude, 2026-09-04, fuori dai tre item).** Sette file erano
stati riscritti da CRLF a LF dalla sessione precedente (`MainComponent.cpp`/`.h`,
`Types.h`, `GrooveEngine.h`, `PercussionEngine.cpp`, `BeatTracker.cpp`,
`ATTRIBUTION.md`): il diff leggeva 11k righe invece delle 928 reali e il blame
sarebbe sparito nel commit. Riportati a CRLF, contenuto invariato. E i due mirror
in `.agents/skills/` erano indietro — quello di `percussion-patterns` citava
ancora `setShakerSubdivision`, che non esiste più da tempo — risincronizzati da
`.claude/skills/`. Ricompilati dopo: `VPTests`, `VPRender` e l'host Release
linkano puliti.

---

### 12. PARTE: dropdown stili (fino a DUE-UNO) + DINAMICA accanto 🟡 (2026-09-04, codice ok — resta touch iPad)

Oggi PARTE è una **riga di quadrati**: AUTO, MARCHA, ROCK, DANCE, POP, SAMBA, FUNK, REGGAE, BOSSA, DUE-UNO, e **DINAMICA** in fondo (`placeSquareRow` in `MainComponent.cpp`). Troppi tasti.

**Layout:** un **select custom** (dropdown, non ComboBox nativo iOS/macOS che rompe il look) con tutti gli stili **fino a DUE-UNO**, e **accanto** il pulsante **DINAMICA così com'è** (toggle `dynamicsFollow`, stesso comportamento). Niente altri controlli in quella riga.

Voci del select, in quest'ordine: **AUTO**, MARCHA, ROCK, DANCE, POP, SAMBA, FUNK, REGGAE, BOSSA, DUE-UNO. Default del motore resta **MARCHA / `grooveAuto` off** — è quello che l'app già spediva (`EngineSettings`, test "the default is an eighth-note marcha"); il detector è misurato a 3/9 e AUTO acceso di default lo renderebbe il suono di un install fresco. AUTO è la prima voce del menu. Scegliere uno stile spegne AUTO (`applyStyle`). Rimettere AUTO riaccende il detector. DUE-UNO resta solo manuale (il detector non lo sceglie mai). Non confondere con AUTO in STRUMENTI (item 6).

**Stile e altezza:** select chiuso e tasto DINAMICA **stessa altezza**, stesso chrome (fill, bordo, tipo, colore acceso/spento come gli altri `TextButton` di PARTE/STRUMENTI). Il menu aperto stesso linguaggio visivo, non un picker di sistema. Con AUTO acceso, il chiuso mostra **AUTO** e accenna lo stile rilevato (stesso tint di prima).

- [x] Sostituire i dieci `style*` button con un controllo custom; tenere `dynamicsButton`. `StyleSelect` + `StyleMenuOverlay` in `MainComponent`.
- [x] Prefs: stessa semantica `grooveAuto` / `grooveStyle`. Default motore invariato (MARCHA, AUTO off) — vedi nota sopra.
- [x] Altezze allineate + stesso stile (fill/bordo/underline fuchsia). Da verificare su iPad; compilato Designed for iPad.
- [x] DSP invariato: `applyStyle` / `applyStyleAuto` come prima, commit al prossimo quarto (item 5 resta un bug a parte).
- [x] Touch: il chiuso prende il resto della riga PARTE; il menu si apre sotto e un tap fuori lo chiude. Non copre START/STOP in modo permanente (PARTE sta sotto TRASPORTO, il menu va in giù).

**Review (Claude, 2026-09-04)** — compila (host Release, zero errori). Il default
MARCHA / AUTO off è la lettura giusta del TODO, non un'interpretazione: la nota
sopra la scrive già. Restano due cose:

- [x] `styleMenuLabel` duplicava `vp::toString (GrooveStyle)`, con il `10`
  ```
  scritto a mano in tre punti: l'ordine coincideva, ma se l'enum cambiava le
  etichette mentivano in silenzio. **Corretto (2026-09-04):** la voce 0 è
  AUTO, le altre leggono `vp::toString (GrooveStyle)`, e il conteggio è
  `StyleMenuOverlay::kCount = 1 + (int) GrooveStyle::count` — usato per la
  dimensione di `items[]`, il ciclo del costruttore e il layout in
  `resized()`. Uno stile nuovo nell'enum ora compare da solo nel menu con il
  nome giusto, invece di un `"?"` silenzioso.
  ```
- [ ] **Ascolto/touch:** STRUMENTI è passata a **dieci** celle quadrate su una riga (erano nove; il tasto SWING dell'item 7 è la decima, 2026-09-07)
  ```
  (`side = (W - 9*gap) / 10`): «NATURALE» e «SWING» in quello spazio accanto a «1/16».
  Da guardare su iPad vero prima di dire che è a posto. Unico residuo.
  ```

---

### 13. Tasto «L'1 è QUI» / «SPOSTA L'1» ✅ (2026-09-04, poi semplificato 2026-09-14)

**Comportamento attuale (2026-09-14):** è un **solo pulsante**, una sola
funzione, una sola etichetta: **«L'1 è QUI»**. Un tocco dichiara che il quarto
su cui sta il clock è l'**uno** — ruota il conteggio di quanto serve per portare
il battito corrente a zero e **blocca** (`BeatTracker::declareBarHere`). Non
sposta più di un quarto in avanti, non è più un toggle: niente «SPOSTA L'1»,
nessuno sblocco dal pulsante. Il fill fuchsia segnala solo che il conteggio è
del musicista (un TAP che dichiara l'uno lo accende lo stesso).

**Storia.** Prima (2026-09-04): «SPOSTA L'1» = sbloccato, un click spostava l'1
di un quarto e bloccava; «L'1 è QUI» = acceso, un tap sbloccava senza nudge.
Su mixer/file il lucchetto **impedisce** il riallineamento automatico (item 2).
Il musicista l'ha trovato confuso (un toggle che sposta, non "questo è l'uno"),
da cui la semplificazione.

- [x] `BeatTracker::declareBarHere()`: `rotateBarIndex(-beatInBarIndex())` +
  `holdBarDecision()`. Il `barNudge` resta nel motore ma non è più raggiungibile
  dalla UI.
- [x] `EngineSettings::barDeclare` (contatore one-shot) consumato in
  `processBlock`, come `notifyInputRestart`.
- [x] Handler `barButton.onClick` = solo `barDeclare.fetch_add(1)`.
- [x] Test riscritto: `"L'1 e' QUI declares the one each press, never nudges and never toggles"`.
  `VPTests --bar` 10/0.

**Review (Claude, 2026-09-04)** — verificato che lo sblocco arriva davvero al
tracker: `VirtualPercussionEngine.cpp` riconcilia `cfg.barLocked` leggendo prima
di riscrivere, quindi lo `store (false)` della UI diventa
`tracker.setBarLocked (false)` invece di essere sovrascritto dalla risposta del
tracker. `BeatTracker` non toccato, `holdBarDecision` intatto. Unico rilievo:
`barControlNudgeOnTap` era una negazione avvolta in una funzione in `Types.h`
con un test tautologico sopra — il comportamento vero stava nell'handler, e il
test non lo copriva. **Rimossa (Claude, 2026-09-04):** inlineato `! locked`
nell'handler in `MainComponent.cpp`, e il test riscritto per girare la stessa
sequenza tap-tap-tap su un vero `EngineSettings` (nudge/lock/unlock/nudge di
nuovo), non su un bool nudo. Verificato PASS con una probe indipendente.

---

### 14. Waveform del brano caricato (seek in impostazioni)

Quando un brano è caricato (`internalPlayer`, `trackTransport`), in **impostazioni**, **sotto** i pulsanti CARICA / PLAY (riga INGRESSO: `sourceButton`, `trackLoadButton`, `trackPlayButton`, …) c'è la **classica onda** da inizio a fine.

Trascinare il dito lungo l'onda = anteprima della posizione. **Rilascio** = riproduce **da quel punto** (`trackTransport.setPosition` + play). Non seek continuo mentre si tiene premuto (niente scratch): il salto è al sollevare il dito.

Senza brano: l'onda non c'è (o è vuota/disabilitata). Playhead che segue il brano mentre gira. Stesso look della pagina SETUP (non un widget iOS nativo).

Il salto è un taglio per il tracker: non lasciare le percussioni sul vecchio punto del brano. Rientro come item 2 (finestra coming-in / epoch), senza restartare il clock del tempo. Cambio di **file** (altro brano) è l'item 3, non questo.

- [x] Peaks dell'onda calcolati al load (fuori dall'audio thread); ridisegnare al resize (`buildTrackWaveform`, 1024 colonne).
- [x] Visibile solo con file caricato, sotto CARICA/PLAY; layout SETUP che cresce (non coprire CLOCK/BUFFER).
- [x] Drag + release → seek + play da lì; playhead in play (`TrackWaveform` + `seekInternalTrack`).
- [x] Tracker/perc: dopo il seek, l'1 e la parte si riallineano al nuovo punto. Seek chiama `engine.notifyTrackSeek()` → finestra `notifyBarReentry` (item 2), **senza** bump di `analysisEpoch`. Coperto dal test mirato `VPTests --bar`.
- [x] Touch: un dito, niente zoom obbligatorio; brani lunghi restano una barra sola inizio→fine.

---

### 15. Pulsanti ÷2 e ×2: servono o si tolgono 🟡 (2026-09-04, ripristinati e testati — resta ascolto)

**Decisione (2026-09-03):** rimossi. Con `tempoOctaveAuto` sempre attivo,
`BeatTracker::updateAutoOctave` tiene il livello metrico; i pulsanti forzavano
solo un override manuale che duplicava confusione. Casi ambigui (es. 50 BPM,
item 1) vanno risolti nel tracker, non con un workaround in UI.

> **RIAPERTO (2026-09-04) — la premessa di quella decisione è stata falsificata.**
> "Casi ambigui vanno risolti nel tracker" presupponeva che il tracker *potesse*
> risolverli. Per almeno una classe di materiale non può, e ora è misurato: una
> battuta straight a 50 BPM e una half-time a 100 BPM producono **lo stesso
> identico segnale** (cassa, hat, rullo, hat, stessa spaziatura, stesso basso
> sotto gli stessi slot). Un correttore che aggiusta la prima rompe la seconda —
> A/B fatto, numeri in `docs/HANDOFF_OCTAVE_50BPM.md` e nell'item 1. Quale dei
> due livelli sia "il tempo" è una **convenzione**, non un dato acustico: nessun
> automatismo può deciderlo, per principio, non per taratura insufficiente.
>
> Quindi la domanda del titolo ("servono o si tolgono") va riaperta con
> un'informazione che nel 2026-09-03 non c'era. Non è un rollback automatico: TAP
> già copre il caso e forse basta. Ma la motivazione scritta sopra non regge più
> così com'è, e va sostituita da una decisione presa sapendo questo.
>
> - [x] **Deciso (utente, 2026-09-04): rimetterli.** «Rimetti ÷2/×2 così se
>   servono li uso.»
> - [x] **Ripristinati** — tre strati, perché erano stati tolti da tutti e tre:
>   - `EngineSettings::tempoOctave` / `tempoOctaveAuto` (`Source/Core/Types.h`),
>   che erano spariti dalla struct;
>   - il motore che li inoltra di nuovo al tracker
>   (`VirtualPercussionEngine::processBlock` → `setTempoOctave` /
>   `setTempoOctaveAuto`, e lo snapshot che riporta il valore vero invece di
>   `true` fisso);
>   - la UI: `halveButton` / `doubleButton` ai lati del numero di BPM, handler,
>   `applyTempoOctave` / `applyTempoOctaveAuto` / `refreshOctaveButtons`,
>   layout a tre colonne e prefs (`tempoOctave`, `tempoOctaveAuto`).
>   Comportamento com'era: premere il livello su cui sei già **torna ad AUTO**
>   (stesso idioma del tasto battuta), il pulsante si accende solo se il livello
>   l'hai scelto tu, e la scelta è salvata fra le sessioni.
> - [x] **Test:** "the halve button reaches the reported tempo" / "and the double
>   button does too" in `Tests/TestAiBeat.cpp`, dentro il percorso veloce
>   `VPTests --octave`. Asserisce la catena intera (impostazione → tracker → BPM
>   riportato) perché è esattamente il punto che si era rotto in silenzio: il
>   tracker aveva ancora l'API, il motore aveva smesso di chiamarla, e nessuno se
>   n'era accorto.
> - [x] **Limite trovato provando, da sapere quando li usi:** l'app riporta solo
>   **50–215 BPM**, poco più di due ottave. Quindi **un tasto dei due è spesso
>   inerte**, per costruzione: sopra ~107 BPM il ×2 chiede un numero fuori range
>   e il valore torna dov'era (misurato: da 120, ×2 chiede 240 e viene ripiegato
>   a 120); sotto 100 BPM è il ÷2 a non avere spazio. Entrambi agiscono solo fra
>   100 e 107. **Il caso che ti serve funziona**: il brano a 50 letto come 100
>   scende a 50 con un tocco di ÷2, perché 50 è esattamente il fondo scala.
>   Se in futuro servisse più margine, si allarga `kMinBpm`/`kMaxBpm` in
>   `TempoEstimator.h` — ma è una modifica che tocca tutto il tracking, non una
>   costante da girare.
> - [ ] **Ascolto:** provare i due tasti su un brano lento reale — è la verifica
>   che il TODO chiede per gli item musicali e che nessuno ha ancora fatto.

- [x] Verificare ascoltando: con AUTO ottava, ÷2/×2 cambiano qualcosa di utile o solo confondono.
- [x] Se inutili: togliere i due pulsanti, layout, prefs `tempoOctave` da UI, `applyTempoOctave` / click handler. Lasciare `tempoOctaveAuto` e il path automatico in `BeatTracker` se ancora usati dal decoder.
- [x] Se si toglie anche `setTempoOctave` utente: test esistenti su ottava manuale da aggiornare o cancellare **solo** quelli del override; non allentare i test di lock/BPM. (`setUserOctave` nel decoder resta per i test `octave-control`.)
- [x] Nessuna regressione su START, TAP, suddivisione, «SPOSTA L'1» — gate eseguito due volte: `575 passed, 1 failed` (`self-leak MIX quiet src`, ottava intermittente 81,3/120 BPM), poi `576 passed, 0 failed`. Il secondo run è verde; il caso intermittente resta annotato, non attribuito a questo item.

---

### 16. Guadagno automatico dell'analisi: ora attenua anche vicino al clipping 🟡 (2026-09-04, codice + gate ok — resta ascolto)

Utente: abbassando o alzando il **volume di ingresso** dall'app, a volte l'ascolto (tempo/battito) sembra migliorare. Causa trovata: il guadagno automatico che normalizza il segnale per la rete (`applyAnalysisMakeup`, target picco 0.20, tetto 24x) era **solo boost** — clampato a un minimo di 1.0, non attenuava mai un ingresso già più forte del target.

Tentativo scartato: simmetria piena (attenuare sempre verso 0.20 come si boosta verso 0.20). Rompe `octave-sweep` a 168 BPM in `Tests/TestAiBeat.cpp` — quel banco sta a un picco di ~0.25, già vicino al target, e una minuscola attenuazione (gain 0.8, ~2 dB) lo fa leggere a metà tempo. 168 BPM è proprio nella zona già documentata nel commento sopra `kMakeupTargetPeak` come sensibile ("letture a metà tempo sopra i 150 BPM"). Sistemare per davvero servirebbe la stessa validazione estesa (tanti brani/stili) usata per tarare 0.20, fuori scope qui.

Fix effettivo (più stretto): attenua **solo** sopra un picco di **0.90** (vicino al vero clipping, che nessun guadagno a valle può disfare), verso 0.90. Tra 0.20 e 0.90 il guadagno resta esattamente 1.0, invariato — tutto l'intervallo già misurato nella tabella del commento (0.04–0.60) resta intoccato.

- [x] Trovata la causa (clamp min a 1.0 in `applyAnalysisMakeup`, `Source/Audio/VirtualPercussionEngine.cpp`).
- [x] Fix stretto: `kMakeupClipGuardPeak = 0.90f`, attenua solo sopra, invariato sotto (righe ~37-66 e ~853-860 di `VirtualPercussionEngine.cpp`).
- [x] Estratto `octave-sweep` in `vpRunOctaveSweepTest` (`Tests/TestAiBeat.cpp` + `TestAiBeat.h`), richiamabile da solo con `VPTests --octave` (~3-4 min invece dei ~5+ min della suite intera) — usare questo per iterare su qualsiasi cosa tocchi il livello di analisi, non la suite intera.
- [x] Verificato con `VPTests --octave`: 168 BPM torna a leggere giusto (gain=1.000, "on it").
- [x] **Gate fatto (2026-09-04):** `VPTests` intera su richiesta dell'utente — **610 passed, 7 failed**, e le 7 sono tutte preesistenti e attese: 2 leak (`no block of it is moved…`, `a leak that does not land on a whole sample…`) e i 5 RED a 50 BPM dell'item 1 (`a 50 BPM bar makes its 100 BPM hi-hat…`, `mixer/file slow kit holds 50 BPM…`, `mixer/file audible 50 BPM quarter…`), lasciati rossi apposta. **Nessuna regressione nuova**: niente dipendeva dal vecchio clamp boost-only.
- [x] **Il trim INPUT non riavvia più l'analisi (2026-09-07).** Le decisioni sul
  livello fisico (`sourceAudible`, dinamica, rientro e `analysisEpoch`) usano il
  residuo dopo canceller diviso per il trim; il make-up continua correttamente a
  vedere il livello realmente consegnato a BeatNet. `VPOps --input-gain`: zero
  epoch aggiuntive, delta +0,07 ms sul percorso iPad e 0,00 ms diretto. Anche
  `--voice-toggle`: zero epoch, +1,80 ms iPad / 0,00 ms diretto.
- [ ] Ascolto: non fatto. Verificare con un ingresso reale volutamente troppo "caldo" (mixer a livello di linea o trim alto) che il sintomo originale dell'utente (regolare il volume per sentire meglio) sia davvero sparito.
- [ ] Se in futuro si vuole la simmetria piena (attenuare sempre verso 0.20, non solo sopra 0.90), serve rifare la validazione a più brani/stili come quella già in commento sopra `kMakeupTargetPeak` — non è un fix da una riga.

---

### 17. L'ottava non cambia più sotto le mani di chi suona 🟡 (2026-09-04, codice + gate ok — resta ascolto)

Segnalato dall'utente: *«se c'è in esecuzione il percussionista e di punto in bianco
dimezza o raddoppia, si incasina tutto»*. Confermato con misura.

**Cosa succedeva.** `BeatTracker::updateAutoOctave` poteva spostare il livello
metrico in qualsiasi momento, anche a brano avviato e parte suonante. Il caso che
lo mostra: una band che suona **a 168 BPM** sta esattamente sul confine
`kOctaveTooFast = 168`, e la normale deriva umana glielo fa attraversare. Traccia:

```
t= 20.3  bpm= 167.49     ← il tempo deriva verso l'alto
t= 23.5  bpm= 168.20     ← supera la soglia
t= 26.7  bpm=  84.24     ← dimezza, a metà brano
t= 58.7  bpm=  83.46     ← e non torna più: per risalire servirebbe
                            scendere sotto kOctaveTooSlow = 49
```

Riproducibile a due decimali su esecuzioni ripetute. In app: BPM mostrato che si
dimezza, densità della parte che cambia, e il percussionista che si ritrova la
griglia spostata sotto le mani.

**Cosa ho cambiato.** Non la soglia e non la politica di quale livello sia giusto:
solo **quando** è lecito cambiarlo. L'ottava automatica ora si muove soltanto se
**non sta suonando nulla** — prima che la parte entri, a uno stand-down, dopo STOP
— e resta ferma per tutta l'esecuzione. Usa `sounding`, lo stesso segnale che il
codice già adopera per non ruotare la battuta sotto chi ascolta.

Tre cose restano aperte apposta:

- **un brano nuovo riparte pulito** (`setInputEpoch` azzera comunque il livello):
non resti incastrato sul livello del pezzo precedente;
- **÷2/×2 funzionano anche mentre suoni** (strada manuale, non automatica): se il
livello congelato è sbagliato, il rimedio è un tocco — è il caso per cui item 15
li ha rimessi;
- **la scelta iniziale resta automatica**, in acquisizione.

**Misure, mixer, materiale con deriva 3 BPM:**


| caso        | prima                                      | dopo                                                                                                        |
| ----------- | ------------------------------------------ | ----------------------------------------------------------------------------------------------------------- |
| **168**     | 83.68, oscillazione **84.98**, mai stabile | **166.90**, oscillazione **2.02**, stabile in 3.6 s                                                         |
| 158         | 157.57                                     | 157.01 — invariato                                                                                          |
| 176         | 88.16                                      | 88.12 — dimezza ancora, ma **in acquisizione** (2 s) e poi fermo: corretto, 176 sta davvero sopra il limite |
| 132         | 132.36                                     | 132.39 — invariato                                                                                          |
| 104         | 103.53                                     | 103.67 — invariato                                                                                          |
| 200 (fisso) | 100.25, 3 salti, 1 battuta rotta           | 100.02, **0 salti**, oscillazione da 2.10 a 0.13                                                            |


- [x] Modifica in `BeatTracker::updateAutoOctave` (`Source/Tracking/BeatTracker.cpp`), con il commento che riporta la traccia sopra.
- [x] Verificato con `VPProbe --trace --live --mixer plain <bpm>` sulla matrice qui sopra. **Comando di regressione:** `VPProbe --trace --live --mixer plain 168` deve finire vicino a 167, non a 84.
- [ ] **Ascolto:** suonare un pezzo a ~168 che deriva e verificare che la parte non cambi densità a metà. Non fatto.
- [x] Gate `VPTests` intera **fatto (2026-09-04)**: 610 passed, 7 failed, tutte preesistenti (2 leak + i 5 RED a 50 BPM dell'item 1). Il congelamento dell'ottava sotto `sounding` non rompe nulla nella suite.

---

### 18. Assestamento lento e imprevedibile 🟡 (2026-09-04, misurato e circoscritto — resta il confine a 170)

Trovato durante l'indagine dell'item 17, **non risolto e deliberatamente non
toccato**. Con lo stesso identico materiale, la stessa corsa dà esiti diversi:


| tempo (fisso) | corse | tempo per assestarsi                                                              |
| ------------- | ----- | --------------------------------------------------------------------------------- |
| 104           | 4     | 2.3 s, 1.7 s, **31.7 s**, e una che non ci arriva mai (oscillazione 9.7, 5 salti) |
| 108           | 1     | **29.0 s**                                                                        |
| 100           | 4     | 1.9, 2.0, 2.1 s … e una da **27.0 s**                                             |


Non è un tempo che rompe il tracker: è variabilità **fra esecuzioni**. La skill del
tempo la descrive già (il worker della rete non pubblica lo stesso numero di
ipotesi a ogni corsa, dipende dallo scheduler; esiste `--sync` proprio per
neutralizzarla nelle misure). Quindi **una parte potrebbe essere artefatto del
banco** — ma trenta secondi per agganciare un tempo comune, se capita sul palco,
si sente.

- [x] **Risposto (Claude, 2026-09-04): non è (solo) artefatto del banco.**
  `scripts/probe_steady_tempo.cpp` gira il decoder **senza rete e senza
  scheduler** — deterministico dato il seme — su tempo costante con deriva 3 BPM
  e jitter 10 ms (le impostazioni `--live`), 300 s, 10 semi per tempo. La
  variabilità resta. Quindi non serve più `--sync` per decidere: il fenomeno è
  nel decoder, non nel worker.

  | BPM                         | corse con uscite (su 10) | errore peggiore | fuori più a lungo |
  | --------------------------- | ------------------------ | --------------- | ----------------- |
  | 60                          | **10**                   | **109%**        | **288 s**         |
  | 75 / 90 / 100 / 110         | 0                        | —               | —                 |
  | 120 / 132 / 140 / 150 / 160 | 1–3                      | 4–5,5%          | 2–3 s             |
  | 170                         | **4**                    | **50%**         | **26,5 s**        |

  Letto per bene, non è "il tracker è instabile":
  - **75–160 è sano.** I blip da 2–3 s al 4–5% sono transitori di acquisizione,
  e il tracciamento della deriva è dentro 1–2 BPM (verificato a 132).
  - **I guasti stanno ai bordi della gamma, e sono metà/doppio, non il tempo.**
  **Attribuzione corretta (stessa giornata, dopo una seconda misura).** In una
  prima lettura avevo dato la colpa a `kOctaveTooFast = 168`: **sbagliato**, quella
  costante sta solo in `BeatTracker.cpp` e questa probe è a livello di decoder, che
  non la vede nemmeno. Il fenomeno è il **livello metrico scelto in acquisizione**.
  Misurato con 20 semi per tempo (rapporto riportato/vero alla prima risposta):

  | BPM          | giusto    | a metà   | al doppio     |
  | ------------ | --------- | -------- | ------------- |
  | 50 / 60      | 2/20      | —        | **18/20**     |
  | 70           | 9/20      | —        | 11/20         |
  | **80 → 160** | **20/20** | 0        | 0             |
  | 170 / 180    | 17/20     | 0        | 0 (3 "altro") |
  | 200          | 13/20     | **7/20** | 0             |

  Cioè: **la gamma dove vive quasi tutta la musica (80–160) è già pulita**, 20 su
  20. Quello che resta è raddoppio sotto i 70 e dimezzamento a 200 — esattamente
  l'ambiguità metà/doppio che l'item 1 ha dimostrato **indecidibile dall'audio**,
  non un bug da riparare. Il rimedio previsto è già in campo: TAP e ÷2/×2.
- [x] **Non inseguire questa con modifiche al tracker.** La prima ipotesi
  (isteresi sul confine dell'ottava) è stata scartata: il confine non è nel
  decoder e la gamma centrale non ha il problema. Tarare qui vorrebbe dire tarare
  sul rumore. Il lavoro che paga è l'**item 19**, che è un bug vero e riparabile.
- [x] **CORRETTO il 2026-09-07: «i blip da 2–3 s a 120–160 sono transitori di
  acquisizione» era sbagliato, ed era un bug vero.** La probe ha sempre avuto un
  flag `verbose` che stampa *quando* avviene l'uscita, e su questa gamma non era
  mai stato usato. Le uscite cadono a **80, 88, 155, 171, 243, 270, 282 e 295
  secondi**: in mezzo alla corsa, non in acquisizione. Causa trovata e corretta —
  vedi **item 21**. Quello che resta di vero in questo item è la parte sui
  **bordi** (raddoppio sotto i 70, dimezzamento a 200): quella è davvero
  l'ambiguità metà/doppio dell'item 1 e non si tocca.

---

### 20. Le congas «escono» dove lo shaker tiene 🟡 (2026-09-07, misurato e corretto sul canceller — resta ascolto)

Segnalato dall'utente: *«se metto lo shaker resta abbastanza allineato, ma se
inserisco le congas tende a uscire e ad avere più difficoltà»*.

**Riprodotto e misurato.** Il canceller del rientro (`subtractSpeakerLeak`)
tagliava il nostro output in **due** bande a ~1,5 kHz, e le due voci stanno
esattamente ai due lati della linea: misurato su otto battute di marcha a
ottavi, lo shaker ha **9,5 dB sopra** la linea, le congas **13,6 dB sotto**. Le
congas ricevevano quindi **un solo guadagno per tutto 0–1,5 kHz**, che è proprio
la gamma che un altoparlante piccolo rimodella di più: passa il corpo e butta
via la fondamentale. Un guadagno solo non può dire «niente di questo e tutto di
quello», quindi trova il compromesso — sottrae troppo sulla fondamentale e
troppo poco sul corpo. Numeri, banco `VPTests --leak` righe `leak-voice`
(fixture stanza a una parete, funk a ottavi, 8 s, quota del nostro rientro
rimossa):

| voce | prima | ora |
|---|---|---|
| solo shaker | 30,4 % rimossa, blocco peggiore 0,88 | 33,8 %, 0,90 |
| **solo congas** | **6,0 % rimossa, blocco peggiore 1,84** | **13,4 %, 1,03** |
| shaker + congas | 14,7 %, 1,06 | 15,9 %, 1,06 |

Il numero che conta è il blocco peggiore delle congas: **1,84** vuol dire che
su quel blocco la sottrazione metteva nell'analisi **più** di quanto togliesse —
il canceller stava aggiungendo un transiente sulla griglia del clock, cioè
esattamente il segnale che conferma al tracker qualunque cosa stia già credendo.

- [x] **Fatto: terza banda.** `kLeakBands = 3`, split a ~250 Hz e ~1,4 kHz (il
  secondo è il vecchio 0.18, lasciato dov'era: la banda dello shaker non
  cambia). Minimi quadrati su tutte e tre insieme via Cholesky con ridge
  **relativo** alla traccia (`kLeakRidge = 1e-2`), non con un fondo assoluto:
  la banda di mezzo, presa come differenza di due poli singoli, ha molta meno
  energia delle due ai lati ed è quella che un feed **senza** rientro riesce a
  spingere. Su mixer, dove il ritorno porta anche il basso, le due bande basse
  si adagiano sullo stesso guadagno e non cambia niente: il fit non ha bisogno
  che gli si dica su che path è.
- [x] **Costo del ridge, dichiarato:** le 54 righe a copia esatta passano da
  0,0000 a **0,0179** (limite 0,10 — un ordine di grandezza di margine invece di
  tre). In cambio il banco no-leak sul path speaker migliora: blocco udibile
  peggiore 1,092 contro 1,111 senza ridge e 1,061 a due bande, e la riga a 128
  frame passa da **11,40 %** (che sforava il proprio limite del 10 %) a 5,13 %.
  Netto: `VPTests --leak` va da 2 fallimenti a 1.
- [x] Banco nuovo, righe `leak-voice`: `roomLeakRun` prende `Voices`
  (shaker / congas / entrambe). Una media sulle due metà non poteva rispondere
  a una segnalazione che parla di una metà sola.
- [x] `VPOps --voices` (ex `--silentpart`, ora quattro passate: muta, solo
  shaker, solo congas, entrambe) sul path iPad a 118 BPM: 6,85 / 0,91 / 1,47 /
  4,96 ms di errore di fase. **Da leggere con cautela**, e la testa di
  `probe_ops.cpp` dice perché: passate indipendenti della rete vera non
  committano lo stesso BPM all'ultimo decimale, e qui la muta esce *peggio* di
  entrambe le voci singole — il rumore fra passate (~6 ms) copre l'effetto. Il
  banco deterministico (`leak-voice`) è quello su cui è stata presa la
  decisione.
- [ ] **Ascolto.** Nessun orecchio umano su questa correzione. Serve rimettere
  le congas su mixer / brano caricato e dire se il problema che si sente è
  ancora lì.
- [ ] **Seconda ipotesi, non ancora misurata, e probabilmente quella che si
  sente su brano caricato.** Con `followSource = internalPlayer` il rientro
  **non esiste** (`process` salta `subtractSpeakerLeak` su `directFile`): lì le
  congas non possono disturbare il tracker, quindi se «escono» anche su file il
  colpevole è un altro, ed è l'**item 2**. Lo shaker è invariante alla
  rotazione della battuta — un pulse è un pulse ovunque cada l'1 — mentre la
  figura delle congas, la frase di otto battute e il fill *dipendono* da quale
  quarto è l'uno. Un 1-vs-3 sbagliato, o una rotazione automatica sotto le mani,
  si sente **solo sulle congas**. Chiedere all'utente su quale fonte l'ha
  sentito prima di lavorarci.
- [x] **Fallimento preesistente risolto (2026-09-07): la ricerca del ritardo
  molla un aggancio buono su un blocco muto.** Isolato con un worktree su HEAD
  pulito (0,2794 anche lì: non era né la terza banda né la sessione parallela) e
  con un probe dedicato che stampa `leakDelaySamples` blocco per blocco. La
  ricerca si rifà da zero ogni quattro blocchi e decide sulla correlazione **di
  quel solo blocco**, mentre il fit del guadagno lì accanto accumula mezzo
  secondo prima di credere a qualcosa. Su una parte rada non è simmetrico: fra
  due colpi il punteggio del ritardo giusto crolla nel rumore, e la periodicità
  della parte stessa lascia picchi d'inviluppo casuali ad altri lag lungo una
  finestra di diecimila campioni. Misurato: lasciava un 8417 corretto e agganciato
  per un 4811 che su un blocco muto faceva 0,1971 contro lo 0,0739 del titolare —
  due numeri senza significato, uno solo nominalmente più grande — e poi vagava
  1,2 s (4811, 9613, 7429, 5633, 8746, 7421) buttando gli accumulatori a ogni
  salto, perché `delay != leakFitDelay` scatta a ognuno.
  **Fix:** la soglia 0,12 resta quella che è, la soglia per *trovare* un percorso
  partendo da niente; **abbandonarne** uno già trovato ora richiede un margine
  (`kDelaySwitchMargin = 0.15`), non di essere nominalmente più grandi.
  `leak-room frac+noise`: **0,2794 → 0,0888** (blocco peggiore 0,99 → 0,13),
  cioè di nuovo lo 0,0957 documentato. `VPTests --leak`: **49 passed, 0 failed**.
- [x] **Effetto collaterale, ed è quello che riguarda la segnalazione:** il
  vagabondaggio colpiva soprattutto la voce più rada. Le congas passano da
  13,4 % a **25,2 %** di rientro rimosso, blocco peggiore da 1,03 a 0,99. Quadro
  completo delle tre tappe:

  | voce | 2 bande (origine) | 3 bande | 3 bande + isteresi |
  |---|---|---|---|
  | solo shaker | 30,4 %, peggiore 0,88 | 33,8 %, 0,90 | 33,8 %, 0,90 |
  | **solo congas** | **6,0 %, 1,84** | 13,4 %, 1,03 | **25,2 %, 0,99** |
  | entrambe | 14,7 %, 1,06 | 15,9 %, 1,06 | 15,9 %, 1,06 |
- [ ] **Gate non lanciati:** `VPTests --makeup` (l'altra metà dello stesso
  argomento: il nostro output che muove la nostra analisi) e `VPTests` intera.
  Costano minuti di worker neurale in tempo reale — chiedere prima.

---

### 21. Le percussioni ogni tanto rallentano o accelerano, e ci mettono a rientrare 🟡 (2026-09-07, causa trovata e corretta — resta ascolto)

- [x] 08/09, segnalazione a ~120 BPM: corretto il passaggio dal recupero rapido
  al filtro ordinario, che conservava la vecchia correzione. Sul clock dopo un
  passaggio sfasato: 3.605 → 0.752 s per tornare stabilmente entro 8 ms; con
  scarto più grande 0.880 s. Durata del recupero derivata dalla distanza e dal
  rail invariato del 20%, scarti confermati <0.25 beat. `probe_recovery`: 84 PASS.
- [ ] Nuovo gate `probe_recovery --slow-passages`: 18 FAIL a 52 BPM, residui
  sotto la soglia di conferma e deadband ordinaria da 13.85 ms. I casi a 256
  fallivano già prima. Restano aperti anche l'attribuzione del problema sul
  brano reale a ~120 BPM e la misura degli attacchi audio: non sono questi
  tempi sintetici a provare il rientro dell'app sulla registrazione.

- [x] 08/09, nuova segnalazione: corretto il recupero che non si confermava
  quando arrivavano seriali distinti a ottavi. I colpi dentro 0.55 beat ora
  conservano il primo candidato e la correzione accumulata. `probe_recovery`:
  18 casi nuovi da FAIL a PASS, 12 precedenti e 5 controlli negativi PASS.
  A buffer 256: scarto <8 ms in 1.733/0.901/0.533 s dalla prima osservazione
  a 52/100/168 BPM, stabile oltre due beat. Solo clock con fase nota;
  il ritardo del riconoscimento sul brano reale resta da localizzare.

- [x] 08/09: sorgente armonica collegata a fase, ingresso e clock sul diretto;
  `--harmonic-entry` 4/4. Con accordi radi l'ingresso resta lento (18.79 s),
  non confondere questa integrazione con riconoscimento istantaneo senza batteria.

- [x] 08/09: timer bassa fiducia e filtro indipendenti dal buffer;
  Recupero anche con fiducia alta: due beat concordanti, 12 casi clock + 3
  controlli mirati; verifica audio completa ancora separata nello step 5.
  `VPTests --state-timing` 3/3 PASS (64/256/1024). Ripresa in HANDOFF_TEMPO.md.

Segnalato dall'utente su un **brano registrato dal vivo**: *«ogni tanto le
percussioni tendono a rallentare o a velocizzare e poi ci impiegano molto a
rientrare nel tempo»*.

- [x] **Rientro quando tornano colpi puliti.** Un fill, un cambio di volume o
  **Aggiornamento 08/09:** il fronte ora arma soltanto; due beat freschi e
  concordanti confermano. Peggioramento e cambio di riferimento annullano.
  `probe_recovery`: 6/6 PASS; mezzo beat misurato dalla conferma, solo clock.
  l'attivazione di un'altra percussione abbassano correttamente la fiducia e
  limitano quanto la griglia segue quell'evidenza; quando la fiducia tornava
  alta, però, il residuo passava ancora dal filtro lento di mantenimento. Ora il
  fronte povero→pulito apre per mezzo beat un recupero continuo, senza snap né
  riavvio del clock. `probe_steer --reentry`, da 0,075 beat di scarto: dentro 8
  ms in **0,579 / 0,312 / 0,253 / 0,179 s** a 52 / 96 / 120 / 168 BPM, intervalli
  fra impulsi **0,99–1,14x**, quindi nessun colpo doppio o saltato. Il banco
  pulito 60 s × 8 semi resta invariato.

**Nota di verifica 08/09:** il vecchio `fillRecovery` misura il ritorno della
velocità, non della fase; non esclude uno scarto persistente. Risultati e limiti
aggiornati in `docs/HANDOFF_TEMPO.md`. Step 1–4 salvati; verifica finale parziale
(detector armonico su audio ora misurato ma non superato; manca registrazione live).
`VPTests --harmonic-audio`: dopo aver allineato il warm-up produzione/probe e
rimosso il doppio gate sulla fonte, il caso senza pad entra a 23.371 s, 99.917
BPM, otto attacchi entro 18.17 ms. Batteria sola e accordo fermo si astengono.
Restano due FAIL: ingresso oltre 4.8 s e caso con pad senza fase valida. Serve
una fonte di pulse non percussivo; non è risolto l'ingresso entro due battute.
Dettagli nel passaggio di consegne.

**Verifica worker reale 08/09:** sui soli stems musicali BeatNet passa 100 BPM
senza pad (2.347 s, 101.940 BPM), ma legge 52 come 103.927 e 168 come 91.585;
con pad falliscono tutti i 52/100/168. Fit e copertura sono buoni anche sulle
ottave errate, quindi non sono distinguibili con un'altra soglia di fiducia.
Ridurre HarmonicTempo a due cambi è stato provato e ripristinato: produce 200
col pad e coerenza 1.0 sulla batteria. Serve la registrazione reale e, se
confermata, un modello adatto agli accompagnamenti tonali; niente euristica
onset è stata lasciata attiva. Misure complete in HANDOFF_TEMPO.md.

**Conclusione storica, limitata alla velocità:** `scripts/probe_steer.cpp`
(nuovo, clock da solo, deterministico) dà una fase sbagliata di 0,25 di beat per
**due secondi** e misura quanto ci mette la griglia a rientrare — **0,06–0,28 s**
a tutte e tre le forze di inseguimento, e ALTO è la più veloce. Lo sterzo di fase
non può produrre un rientro lento.

**È il decoder, ed è il rilevatore di cambi di tempo bruschi che scatta sul
jitter.** `probe_steady_tempo --verbose` su tempo costante, impostazioni
`--live`, 300 s × 10 semi: **otto uscite** fra 110 e 160 BPM, tutte **4,0–5,5 %**
per **1,8–3,0 s** — a 120 BPM è la griglia che va a 125 e torna. Tracciato il
momento della conferma: **il comb leggeva il tempo giusto** (121,21 contro 121,48
vero) mentre la transizione pubblicava **126,85**, e i due fit leggevano `0.00`
perché la transizione li aveva appena buttati. Nessuno chiede al comb.

Due difetti, entrambi nei termini del file stesso:

1. **La soglia per aprire un candidato stava sotto il pavimento dichiarato.**
   `needed = max(1 BPM, 3 × jitter)`; col jitter reale (~1,4 %) vale **4,2 %**,
   mentre `kTransitionSmallestStep` dice duecento righe più su che **5 %** è «il
   più piccolo cambio che valga la pena rivendicare». Fra i due c'era una fascia
   di dimensioni su cui il detector agiva e che non era disposto a difendere.
2. **La prova di coerenza era quattro volte più larga di come si legge.**
   `deviation` è **metà** della differenza relativa e la tolleranza era **2 ×
   jitter**: due intervalli potevano discordare di **4 × il jitter** ed essere
   chiamati «un tempo solo». Misurato alle conferme false: coppie come
   **0,4635 s e 0,4826 s — distanti il 4,1 % fra loro, quanto il gradino che
   rivendicavano.** Non è un periodo misurato due volte; sono due numeri diversi
   la cui media capita lontano dal tempo commesso.

- [x] **Fix, due righe in `BeatDecoder::observeTransition`:** `needed` non scende
  mai sotto `kTransitionSmallestStep`; la tolleranza passa da `2 × jitter` a
  `1 × jitter` (i pavimenti assoluti restano dove sono — non erano loro a essere
  larghi). Un gradino vero supera la barra a due volte lo scatter circa cinque
  volte su sei, e se manca ha subito la coppia dopo: il codice ricade su un
  nuovo candidato invece di aspettare un beat.
- [x] **Effetto: 8 uscite → 3.** 110, 120 e 132 BPM ora **puliti su 10 corse da
  300 s ciascuno**. Restano 144 e 160 con una a testa: le loro due coppie
  concordano davvero entro il jitter, e passano per il pavimento assoluto
  (`kTransitionLineCoherence`). Abbassare quello mette a rischio i gradini su
  materiale pulito — non l'ho toccato.
- [x] **Costo: nessuno misurato.** `probe_tempo_step` è **identico riga per riga**
  prima e dopo (A/B contro `git show HEAD:`). `VPAlign`: i cinque gradini protetti
  tutti **PASS**, ±1 BPM in **0,78–1,47 s**, fase **23,3–24,4 ms**, zero violazioni
  di impulsi — gli stessi numeri che la skill documenta; le rampe restano a
  `transizione=0`. `--octave focused` 6/4 con le quattro rosse deliberate
  dell'item 1, `--swing` 3/0, `--leak` 49/0.
- [ ] **Ascolto.** È l'unica verifica che non posso fare io: rimettere lo stesso
  brano live e dire se le escursioni si sentono ancora.
- [ ] **Aperto, misurato ma non toccato: il default spedito è ALTO.** `Types.h` e
  `MainComponent` impostano `FollowStrength::high`, mentre la tabella della skill
  chiama MEDIO il default. Su materiale live simulato ALTO fa escursioni **2–3
  volte più grandi** di BASSO (peggiore 6,7 % contro 2,6 % a 144 BPM; **0,55
  contro 0,06 secondi al minuto** oltre il 2 %) a parità di rms. Su questo banco
  non compra niente — ma il banco non contiene un cambio di tempo vero, che è
  l'unica cosa per cui ALTO esiste, quindi **non ho cambiato il default**. Serve
  o un banco onesto sul recupero, o l'orecchio.
- [ ] **Restano le 3 uscite a 144/160.** Il discriminante che manca è il comb, che
  aveva ragione tutte le volte — ma al momento in cui il candidato si apre il comb
  è ancora sul tempo vecchio anche su un gradino vero, quindi lì non separa. Per
  usarlo servirebbe *confermare e poi ritrattare* se il comb non corrobora entro
  il suo tempo di assestamento: è la stessa forma del watchdog già provato e
  ritirato (vedi il commit 8528d4e), e non va ritentata alla cieca.

---

### 22. Ai tempi lenti «esce e non tiene», l'1 arriva tardi, e con lo swing l'aggancio costa 13 s 🟢 (2026-09-07, corretto — resta ascolto sul brano dell'utente)

Segnalato dall'utente: *«spesso esce e non tiene, brani tipo a 60 o 80 bpm. Quelli
più veloci sembra vadano meglio. In più il primo quarto a volte lo riconosce ma
molto in ritardo.»*

**I due sintomi sono lo stesso sintomo.** Se la griglia è a doppia velocità, la
battuta viene contata su una griglia sbagliata e l'1 non può essere trovato.

**Misurato** (`probe_steady_tempo`, decoder da solo, deterministico): a **60 BPM
10 corse su 10** leggono il doppio e non rientrano mai; a **70 BPM 9 su 10**
raddoppiano e rientrano dopo 0,7–16 s — è il «esce e non tiene»; **80, 90, 100
puliti**. Nota che quella probe genera **un impulso per battito e silenzio in
mezzo**: raddoppiare lì vuol dire mettere metà griglia sul silenzio, quindi **non
è** il caso indecidibile dell'item 1, dove gli ottavi ci sono davvero.

Tracciato: **il comb leggeva 61,29 a salienza 1,00 per tutta la corsa** mentre il
pubblicato era 122,21, e `octaveMismatch` restava **0**. Tre difetti in fila, uno
dentro l'altro:

1. **La valvola di sfogo era bendata.** `combDisagrees` confronta `combBpm`, che è
   `foldToAnchor (tempo.bpm())` — la lettura del comb **già ripiegata sull'ottava
   in uso**. Il test che esiste per accorgersi di un errore d'ottava lo faceva su
   un valore a cui l'ottava era appena stata tolta: `log2(122,2/122,6) = 0,004`
   contro una soglia di 0,25. Nessun disaccordo, mai. È la stessa trappola che
   `checkGridPhase` documenta per la *fase* («una griglia che si àncora sul
   levare non è solo sbagliata, è stabile… e non c'era niente nella catena che
   potesse accorgersene»), risolta lì mettendo il fold **fuori** dal gate; per il
   *ritmo* il fold **era** il gate.
2. **La guardia `unprovenSlowerOctave` usava come prova a favore una cosa che
   dichiara priva di valore.** Il commento sopra dice: *«una griglia doppia può
   sembrare sana perché ogni battito rilevato cade su un tick sì e uno no»* — e
   poi usa `gridHealthy` per vietare la correzione. Il dato che manca era lì
   accanto, gratis: `coverage = keep/n` vede una griglia **troppo lenta** (deve
   buttare metà eventi) ma non una **troppo veloce**, che li tiene tutti. Il tell
   sono gli **indici** su cui cadono: 0, 2, 4, 6 invece di 0, 1, 2, 3.
3. **E anche quando lo scatto partiva, era un no-op.** `bpm = clamp (combBpm…)`
   adottava di nuovo il valore ripiegato: buttava via la storia dei battiti e
   ricommetteva **lo stesso tempo doppio**. E non spostava `anchorBpm`, quindi la
   riacquisizione successiva ripiegava subito indietro.

- [x] **Fatto:** `fitPeriod` restituisce il **salto mediano di indice** dei battiti
  tenuti (1 su una griglia al polso, 2 su una raddoppiata; misurato: 2,0 esatto a
  60 BPM con coverage 1,00 e residuo 0,030). La guardia lo richiede denso prima di
  difendere la griglia. `combDisagrees`, il voto e lo scatto usano il comb
  **grezzo** (`combRawBpm`); lo scatto sposta anche l'ancora. Tutto il resto
  continua a usare il comb ancorato.
- [x] **Effetto, `probe_steady_tempo` 300 s × 10 semi**, contro la tabella
  dell'item 18:

  | BPM | prima | ora |
  |---|---|---|
  | 60 | 10/10, **99,7 %** del tempo fuori | 10/10, **80,9 %** |
  | 70 | 9/10 | **3/10**, 1,1 % |
  | 75–140 | 120/132/140 con 1–3 corse | **0/10 tutti** |
  | 150 / 160 | 1–3 | 1 / 2, 0,1 % |
  | **170** | **4/10, errore 50 %, 26,5 s fuori** | **1/10, 4,4 %, 0,1 %** |
- [x] **Nessuna regressione sui gate:** `VPAlign` cinque gradini protetti tutti
  PASS con gli stessi numeri (0,78–1,47 s, 23,3–24,4 ms, zero violazioni), e il
  100→140 non protetto migliora da 26,70 a 15,14. `--octave focused` 6/4 con le
  quattro RED deliberate dell'item 1, `--swing` 3/0, `--leak` 49/0.
  `probe_tempo_step`: 100→160 da 27,8 a 21,0 s; 75→140 da 8,1 a 10,7 s (peggio di
  2,6 s); 60→120 da 0,0 a 7,5 s, che **non è una regressione** — prima lo zero
  voleva dire «leggeva già 120 mentre il brano era a 60», cioè era giusto per il
  motivo sbagliato.
- [x] **Chiuso il feedback HMM che rendeva permanente il doppio a 52/60 BPM.**
  La prima misura reale dell'intervallo ora raffina un'acquisizione HMM ancora
  provvisoria; lo scatto d'ottava ricentra anche il marginale di tempo dell'HMM
  senza perdere la sua distribuzione di fase; un HMM su un'altra ottava non può
  più riscrivere da solo `anchorBpm`. Probe deterministica mirata (drift live 3
  BPM, jitter 10 ms): primo lock corretto a 52 BPM **~12,5 -> 3,48 s**, cioè
  tre quarti con il nuovo gate di contesto; su 120 s
  x 5 semi, tempo fuori tolleranza **0,4% a 52** e **1,5% a 60**; 120/160 BPM
  zero uscite su 3 semi. `VPTests --tempo-slow`: 7/0; dopo sei secondi senza
  battiti, il primo ritorno a 26,56 s è sulla fase esatta (0,000 beat). Resta
  volutamente l'ambiguità acustica dell'item 1 sui
  brani lenti con ottavi forti: lì il test ONNX 50 BPM continua a poter leggere
  100 e il comando ÷2 è la soluzione deterministica.
- [x] **Lo swing ai tempi lenti è il caso specifico dell'utente, ed è molto
  peggiore del dritto (2026-09-07).** Segnalato: *«sto ascoltando un brano live
  con swing a circa 81 bpm… tende ad accelerare o decelerare e prima di rientrare
  passano un bel po' di battute. Vorrei che agganciasse il più veloce
  possibile.»* Riprodotto generando battito forte + **ottavo swingato più
  debole** (la probe fino a qui faceva solo impulsi equidistanti):

  | materiale | 81 BPM | 96 BPM | 120 BPM |
  |---|---|---|---|
  | dritto | **1,9 s** | 1,4 s | 1,4 s |
  | swing 0,65 | **13,1 s** (peggio 14,4) | 13,5 s | 3,4 s |
  | swing 1,00 | **17,7 s** (peggio 30,0) | 10,0 s | 1,5 s |

  E stabilità: a 81 BPM il dritto ha 1 corsa su 10 con uscite, lo swing **7 su
  10** con errore peggiore 64 %.

  **Perché 120 è immune:** il livello degli ottavi a 120 sarebbe 240 BPM, oltre
  `kMaxBpm = 220`, quindi non è un'ipotesi legale. A 81 sarebbe 162, dentro. Il
  problema è **lento + swing**, non lo swing.

  **Dove vanno i 13 secondi**, misurato: l'acquisizione si aggancia al livello
  **1,5×** che lo swing implica (123 BPM su 82 veri), e poi il rientro paga due
  attese in fila — il comb non è **pronto** prima di ~8 s a 81 BPM (gli servono
  cinque periodi dell'ottava sotto: la skill lo dice, 7,9 s a 76 BPM) e poi
  servono ~6 battiti di voti, altri 4,4 s. **La metà più grande è strutturale**,
  non una soglia da stringere. Il comb intanto leggeva 82,19 a salienza 1,00 dal
  primo istante in cui poteva parlare.
- [x] **Fatto, piccolo ma coerente:** il bonus di pazienza `kOctaveSnapBeatsHealthy`
  non va più a una griglia che usa un tick sì e uno no. È la stessa
  contraddizione del punto 2 sopra, nella stessa funzione: il commento dice *«il
  doppio del tempo vero cade su ogni picco rilevato, quindi è sempre una di
  quelle sane»* e poi concedeva pazienza proprio per quello. Vale 19,0 → 17,7 s
  sullo swing pieno a 81; non è il termine dominante e non pretendo che lo sia.
- [x] **Aggancio swing risolto nel punto in cui nasce.** Il decoder ora riconosce
  la cella causale lungo-corto: sul bus diretto bastano forte-debole-forte; sul
  microfono serve il quarto picco, che corrobora la seconda cella e respinge una
  coppia transiente/riflessione. Prima della griglia il refractory usa il periodo
  più veloce legale, quindi non cancella il ritorno corto dello swing pieno; l'HMM
  non può pubblicare una risposta senza contesto un istante prima che la cella si
  chiuda. Probe mirata, ratio 0,60 e 2/3 a 52/81/96/120/168 BPM: primo tempo
  corretto in **1,04–1,40 quarti sul diretto** e **1,37–2,07 sul microfono**;
  tutti ancora corretti dopo 10 s. Il vecchio percorso da 13–18 s non parte più.
- [x] **L'1 entro due battute.** I voti di battuta si accumulano e decadono **per battito**
  (`kVoteDecay = 0.982`), quindi `voteBeats` satura a **55,6**. Con
  `kBeatsToMoveTheBar = 32` servono **47 battiti accettati** prima di poter
  spostare l'1 mentre si suona, ma la decisione d'ingresso e il rientro ora
  attraversano la soglia al **quarto numero 8**:

  | | battiti | a 60 BPM | a 80 BPM | a 120 BPM |
  |---|---|---|---|---|
  | in ingresso / rientro (soglia 7 con decay) | 8 | 8,0 s | 6,0 s | 4,0 s |
  | **in play (`kBeatsToMoveTheBar = 32`)** | **47** | **47,2 s** | 35,4 s | 23,6 s |

  Il margine di vittoria non è stato abbassato: entro due battute si agisce solo
  se l'opinione è chiara. Durante il play resta la soglia prudente di 32, perché
  cambiare il conteggio sotto una parte già udibile è un'operazione diversa.
  `VPTests --bar`: 10/0, incluso il rientro dopo un taglio di due quarti.
- [x] **100 non diventa più 50/200 e i pulsanti non saltano due ottave.** La
  vecchia cadenza a soglia interpretava due downbeat mancati a distanza di otto
  quarti come prova di una griglia doppia: su un 100 chiarissimo poteva quindi
  pubblicare il suggerimento 50. Quel discriminante, già dimostrato ambiguo, è
  ora davvero spento. Inoltre ÷2/×2 avanzano di un livello dal valore
  **effettivamente visualizzato**, non impostano più −1/+1 assoluti: da 50 si va
  a 100 e poi 200; da 200 si torna a 100 e poi 50. Regressione mirata dentro
  `VPTests --tempo-slow`: 100 resta 100 con downbeat alterni mancanti e le quattro
  transizioni 100↔50 / 100↔200 passano tutte da 100.

---

### 23. Il regime FISSO si congelava su una band che deriva 🟡 (2026-09-07, trovato e corretto — resta ascolto)

Richiesta dell'utente: *«verifica e potenzia al massimo il riconoscimento dei bpm
nel modo più veloce possibile, e se sta uscendo dal tempo fallo rientrare subito,
come un perfetto percussionista capirebbe al volo»*.

**Prima cosa, per chi legge dopo:** durante questa sessione HEAD si è mosso
(commit `7292ffb` e `74fe5fc`), quindi le misure degli item 21 e 22 sono contro
un albero più vecchio. Il tabellone qui sotto è il tree attuale.

**Banco nuovo: `scripts/probe_matrix.cpp`.** Aggancio *e* stabilità, su
dritto / swing 0,65 / swing 1,0 × 60…170 BPM × 10 semi. Genera anche l'**ottavo
swingato**, che `probe_steady_tempo` non fa e che è la metà del problema
dell'utente. Non è in CMake, si compila in due secondi (comando nel suo header).

Stato del tree **prima** di questo item: **aggancio medio 1,78 s** su tutta la
matrice, righe swingate già pulite, 60 BPM che non raddoppia più. Restavano **5
uscite su 210 corse**, tutte al 4,2–4,4 %, da 1,4 a 10 secondi.

**Tracciata la peggiore** (dritto 81 BPM, seme 7, 10 s fuori) e non era niente di
quello che avevo corretto finora:

```
t=20.5  pubbl=82.09 (vero 80.87)  reg=FISSO  comb=82.08  lungo=82.20  corto=81.79
t=23.4  pubbl=82.09 (vero 80.23)  reg=FISSO  comb=81.30  lungo=82.01  corto=80.93
```

Il pubblicato **congelato a 82,09** mentre il tempo vero scende. Comb, fit lungo
e fit corto lo seguono giù tutti e tre: **solo il numero pubblicato non si
muove.** Il decoder aveva deciso «disco tagliato a click» su una band che deriva
di 3 BPM — entrando in FISSO in un tratto piatto della deriva — e poi ci è
rimasto. Su un brano **live** è il guasto peggiore che ci sia: l'app smette di
seguire la band.

- [x] **Difetto 1: il contatore d'uscita si azzerava al primo battito buono.**
  `if (wandered) ++fixedErrorBeats; else fixedErrorBeats = 0;` e l'uscita ne
  vuole 6 **consecutivi**. Su una deriva l'errore oscilla attorno alla soglia,
  quindi i sei consecutivi non arrivavano mai. È **la stessa identica lezione**
  che lo scatto d'ottava ha già imparato duecento righe più giù, col suo
  commento: *«un contatore che si azzerava al primo battito d'accordo non
  arrivava da nessuna parte… è così che un livello scelto nei primi secondi
  sopravviveva a ogni correzione.»* Ora decrementa invece di azzerare.
- [x] **Difetto 2, quello che conta: l'uscita da FISSO guardava solo la
  *dimensione* dell'errore, mai la *direzione*.** Tutti e quattro i termini
  d'uscita sono «quanto ci siamo allontanati», e su una deriva la dimensione
  arriva tardi per costruzione — l'errore parte da zero e cresce, e quando il 2 %
  si è accumulato il musicista è fuori da secondi. Il flag `moving` («gli estremi
  della finestra differiscono, e più della sua stessa dispersione, quindi è una
  direzione e non rumore») era già calcolato lì sopra e **usato solo in
  acquisizione**. Ora libera anche FISSO. Un disco a click non può produrlo; una
  band lo produce prima che l'errore si senta.
- [x] **Effetto:** dritto 81 BPM da **1/10 corse con uscite (10 s fuori al
  4,3 %) a 0/10**. Totale 5 → 4 uscite, tempo fuori medio **0,06 % → 0,03 %**.
  E le **rampe migliorano** in `VPAlign`: fase 20,8 → 18,8 ms sulla rampa da 4 s
  e 8,0 → 6,0 ms su quella da 12 s — coerente, uscire da FISSO su un tempo che si
  muove vuol dire seguirlo meglio.
- [x] **La proprietà opposta è intatta, misurata apposta:** su click track vero
  (deriva 0, jitter 0,5 e 2 ms, 81/96/120/140 BPM, 8 semi) errore medio
  **0,004–0,023 % e zero uscite, identico a HEAD riga per riga**. `moving` non
  scatta su un click.
- [x] **Gate:** `probe_tempo_step` identico a HEAD su tutte e 11 le righe;
  `VPAlign` cinque gradini protetti identici (0,78–1,47 s, 23,4–24,4 ms, zero
  violazioni di impulsi); `--octave focused` 6/4 con le quattro RED deliberate
  dell'item 1; `--swing` 3/0; `--leak` 49/0.
- [ ] **Restano 4 uscite su 210**, tutte su materiale **dritto**: 60 BPM 3/10 e
  170 BPM 1/10, al 4,2–4,4 %. Il materiale dell'utente (lento e swingato) è
  **pulito 0/10 su tutte le righe**. Non le ho inseguite: sono ai due bordi della
  gamma e valgono lo 0,03 % del tempo.
- [x] **Il banco stretto dava numeri falsamente rassicuranti, e l'utente ha avuto
  ragione a rifiutare la taratura su un brano preciso.** Chiesto esplicitamente:
  *«non voglio venga fatto su un brano preciso, ma sempre, sia con brani caricati
  sia in live»*. Giusto, ed è anche quello che dice il resto di questo file. Il
  banco è stato allargato da due assi (tempo, swing) a **dodici materiali** —
  `scripts/probe_matrix.cpp`, con densità della suddivisione, accenti per quarto,
  jitter, battiti ingoiati dal mix e vuoti d'arrangiamento. Il quadro cambia:

  | materiale | aggancio med / peg | uscite | %fuori |
  |---|---|---|---|
  | metronomo | 2,31 / 6,98 | 1/30 | 0,05 % |
  | **rock a ottavi** | **7,30 / 29,80** | **9/30** | **8,30 %** |
  | rock a sedicesimi | 2,41 / 6,98 | 1/30 | 0,03 % |
  | backbeat secco | 4,27 / 17,36 | 9/30 | 0,42 % |
  | **half-time** | **mai** | **30/30** | **98,95 %** |
  | swing 8vi / shuffle 16mi / swing pieno | 2,5–4,4 | 1–3/30 | ≤0,10 % |
  | **band larga (25 ms)** | 7,53 / 31,84 | 10/30 | 0,68 % |
  | **mix che ingoia (18 %)** | **9,05 / 49,40** | **16/30** | 4,54 % |
  | **con vuoti (1 battuta su 4)** | 9,61 / 35,46 | 9/30 | 2,00 % |
  | solo accordi | 4,97 / 19,56 | 10/30 | 1,31 % |

  **Aggancio medio reale 5,30 s, non 1,78.**
- [x] **Come sbaglia, non solo quanto** (`probe_matrix --ratio`, rapporto medio
  riportato/vero). Quasi tutto legge **1,00**, cioè il livello giusto — il che è
  la notizia buona. Due eccezioni:
  - **rock a ottavi a 81 BPM: 1,44.** Media fra corse giuste e corse sull'ottava
    sbagliata. È lo **stesso caso** dello swing lento dell'item 22: tempo lento +
    suddivisione riempita, dove il livello degli ottavi (162) è ancora dentro la
    gamma. A 100 BPM e oltre sparisce, perché sarebbe ≥200.
  - **half-time: 2,02 ai lenti, 0,50 ai veloci.** È l'ambiguità metà/doppio
    dell'item 1, **e in più la fixture è ingiusta per una probe solo-decoder**:
    la convenzione che tiene il polso nella gamma di un percussionista vive in
    `BeatTracker::updateAutoOctave`, che questa probe non esercita. Non leggere
    il 98,95 % come un guasto senza prima rifarlo end-to-end.
- [x] **Le due modifiche di questo item verificate anche sul banco largo**, A/B
  contro `git show HEAD:`: uscite totali **104 → 101**, fuori medio
  **9,83 % → 9,70 %**, aggancio medio invariato, e **nessuna cella peggiora**
  (metronomo 2/30→1/30, rock 16mi 2/30→1/30, mix che ingoia 17/30→16/30,
  half-time 99,49→98,95 %). Miglioramento piccolo e pulito, non una svolta.
- [ ] **La classifica di cosa attaccare dopo, in ordine di quanto pesa.** Tutte e
  tre le prime sono *acquisizione*, non recupero:
  1. ~~**Rock a ottavi ai tempi lenti (~81 BPM): 8,30 % del tempo fuori.**~~
     **FATTO (2026-09-07), vedi sotto.**
  2. **Mix che ingoia battiti: aggancio 9,05 s, peggiore 49,4 s.** Un missaggio
     vero non consegna ogni battito.
  3. **Vuoti d'arrangiamento e band larghe: 9,6 e 7,5 s.**
- [x] **Punto 1 chiuso: il charleston sugli ottavi teneva la griglia un'ottava
  sopra, per sempre.** Tracciato `rock 8vi` a 81 BPM, seme 2:

  ```
  t=11.1  pubbl=163.85 (vero 82.48)  comb=82.08 sal=1.00  lungo=163.87  mism=0  gap=1.0
  t=59.4  pubbl=164.67 (vero 81.15)  comb=82.30 sal=1.00  lungo=164.58  mism=0  gap=1.0
  ```

  Il fold diceva **82 a salienza 1,00 per un minuto intero**, il committed stava
  a **164** — un'ottava esatta — e `octaveMismatch` non lasciava mai lo zero.
  Causa: il veto `unprovenSlowerOctave`. Il `gridIsDense` dell'item 22 prende la
  griglia raddoppiata **quando fra i battiti c'è silenzio**; qui il charleston
  *riempie* quei tick, quindi la griglia sbagliata è densa, coverage 1,00,
  residuo 0,03, salto d'indice 1,0 — ogni prova che il veto possiede dice che la
  griglia va bene.
- [x] **Fix: quello che le due griglie non condividono è il *peso* dei battiti.**
  Sul polso sono tutti battiti; un'ottava sopra, uno sì e uno no è un charleston.
  Nuovo `BeatDecoder::recentStrengthAlternation()` — mediane per parità sugli
  ultimi 12 battiti accettati, misurato **0,1–0,2 su una griglia giusta contro
  ~0,5 su una costruita sulla suddivisione**. Il veto si stende quando
  l'alternanza supera 0,35 **e** il fold nomina qualcosa entro il 20 % di metà
  del committed (`kSubdivisionAlternation`, `halfError`). **Solo toglie il
  veto**: lo scatto continua a volere la salienza del fold e i suoi `snapBeats`
  di voti.
- [x] **Effetto sul banco (12 materiali × 5 tempi × 6 semi):**

  | | prima | dopo |
  |---|---|---|
  | **rock 8vi, tempo fuori** | **8,30 %** | **0,65 %** |
  | rock 8vi, rapporto a 81 BPM | **1,44** | **1,00** |
  | con vuoti | 2,00 %, agg 9,61 s | 1,50 %, agg 8,72 s |
  | mix che ingoia | 4,54 % | 4,38 % |
  | mai-agganciato | 33 | 30 |
  | **fuori medio, tutto il banco** | **9,70 %** | **9,01 %** |

  **Nessuna cella peggiora**, e nella tabella dei rapporti **ogni riga tranne
  half-time legge adesso 1,00 a tutti e cinque i tempi**.
- [x] **Gate, tutti verdi e tutti A/B contro `git show HEAD:`:** `probe_tempo_step`
  **identico riga per riga**; click track (deriva 0, jitter 0,5 e 2 ms)
  **identico riga per riga** — l'alternanza non scatta su un click; `VPAlign`
  cinque gradini protetti tutti PASS con gli stessi numeri (0,78–1,47 s,
  23,4–24,4 ms, zero violazioni di impulsi) e rampe PASS; `--octave focused` 6/4
  con le quattro RED deliberate dell'item 1; `--swing` 3/0; `--leak` 49/0.
- [ ] **Segnalazione, non mia:** `VPTests --bar` è **9/1 sul tree attuale** —
  `within two bars of the return the one is beat zero again` — ed è rosso anche
  con le mie modifiche tolte (verificato con `git stash`). Preesistente, item 2.
- [ ] **Restano, nell'ordine:** `mix che ingoia` (aggancio 8,98 s, peggiore
  49,4 s, 4,38 % fuori — un missaggio vero non consegna ogni battito),
  `band larga` a 25 ms (7,53 s, 0,68 %), `con vuoti` (8,72 s). E half-time, che
  però prima di essere chiamato guasto va rifatto **end-to-end**: la convenzione
  che lo decide sta in `BeatTracker::updateAutoOctave`, che questa probe non
  esercita.

- [ ] **Non tarare niente di tutto questo su un brano solo.** Il banco esiste
  apposta; ogni modifica va misurata su tutte e dodici le righe **e** su
  `probe_tempo_step` + `VPAlign`, come queste due.
- [ ] **Ascolto.** È l'unica verifica che manca: rimettere lo stesso brano live
  swingato a 81 e dire se il congelamento si sente ancora.

---

### 24. A basso livello l'acquisizione prende un'ottava alta falsa 🟡 (2026-09-08, causa trovata e corretta a -12 dB — resta -18 dB e l'ascolto)

Riprodotto sulla registrazione dell'utente (`3 INFINITO.mp3`, estratto 30-120 s,
riferimento 91 BPM). Stesso contenuto musicale, solo il livello cambia:

| livello | avvio | `FISSO` | media finestra | deriva media / max |
|---|---|---|---|---|
| `-f 65536` (caldo) | 91 subito | — | 91.06 | 5.3 / 19.0 ms |
| `-f 8192` (circa -12 dB) | 212.6 BPM, snap a 91.19 solo a 12 s | ~20 s | 94.94 | 15.7 / 257.7 ms |

Non e' "il volume alto fa perdere il tempo": e' il **livello basso** che lascia
passare una falsa ottava alta durante l'acquisizione. Tenuto il tempo, il
mantenimento e' buono in entrambi i casi.

**Misurato il frontend (nuovo `VPActivations --wav ... --sweep`).** Primi 12 s
dell'estratto, guadagno applicato **solo alla copia destinata al modello**:

```bash
cmake --build build-host --target VPActivations -j4
./build-host/VPActivations_artefacts/Release/VPActivations \
  --wav /tmp/vp-infinito-30-120.wav --sweep --secs 12 --gains 0,-6,-12,-18
```

| gain | rms | magMean | diffMean | pBeat>0.5 | pDown medio | pDown>0.5 |
|---|---|---|---|---|---|---|
| 0 dB | -19.5 | 0.2395 | 0.0301 | 33 | 0.035 | 17 |
| -6 | -25.5 | 0.1595 | 0.0216 | 23 | 0.084 | 30 |
| -12 | -31.5 | 0.1020 | 0.0148 | 15 | 0.144 | 57 |
| -18 | -37.5 | 0.0624 | 0.0096 | 3 | 0.169 | 67 |

I fotogrammi di battito confidenti **crollano da 33 a 3** e le attivazioni di
downbeat **quintuplicano**: al decoder arriva un'evidenza diversa, non piu'
debole in modo uniforme.

**Il frontend non ha un bug di normalizzazione.** Verificato con un seno a
1 kHz a fondo scala: `magMax = 2.2666`, cioe' `log10(1 + 183.6)`, contro il
riferimento madmom analitico `|X|` di picco `352.1` (finestra `/ 32767`, rfft
non normalizzata) ridotto dal filtro triangolare a somma 1. Scala, finestra e
filterbank coincidono con BeatNet/madmom. madmom non e' installato qui, quindi
**non** e' stato fatto un confronto bit-a-bit con l'implementazione originale.

La dipendenza dal livello e' **intrinseca a `log10(1 + x)`**: lo stesso seno
scende da 2.2666 a 1.3826 a -18 dB. Nessuna AGC nel bus audio; un eventuale
condizionamento riguarda solo la copia del segnale per il modello.

- [x] **Arbitraggio iniziale corretto (2026-09-08).** Causa: `tryFastAcquire`.
  A -12 dB i picchi distavano 141 ms (424 BPM); il test di alternanza concludeva
  giustamente «è una suddivisione» e raddoppiava **una sola volta**, a 212, che
  supera lo stesso test. I rami a coppie e ad alternanza azzerano `bestError`,
  disattivando il confronto con lo state space — che stava dicendo 103.45, un
  ottava esatta di distanza. Fix: sopra `kFastAcquireVetoBpm` (180) lo state
  space conserva un veto oltre `kOctaveThreshold`. Sotto la banda nulla cambia.

  | livello | prima | dopo | deriva media | peggiore |
  |---|---|---|---|---|
  | clip / 0 / -6 dB | corretto | invariato | invariata | invariata |
  | **-12 dB** | 94.92 BPM | **90.96** | 16.3 → **9.5 ms** | 262.5 → **139.4 ms** |
  | -18 dB | 121.10 | 121.06 | invariata | invariata |

  Regressioni prima/dopo: `probe_matrix` (360 corse) **identico byte per byte**,
  `probe_tempo_step` identico, `VPAlign` identico, `--tempo-slow` 10 PASS,
  `--octave` 7/4 identico (i 4 fallimenti a 50 BPM sono l'item 1, preesistenti).
- [x] Aggiunto `VPLive --gain <dB>`: la prova di livello è un ciclo su un solo
  file, senza pre-renderizzare WAV con mpg123.
- [ ] **-18 dB resta rotto, ed è un altro guasto**: aggancia 91 entro 12 s, poi
  è il **pettine** a saltare a 182.37 per quattordici secondi prima di tornare.
  Salto d'ottava dopo l'acquisizione, sorgenti concordi sul valore sbagliato.
- [x] **Regressione mirata del livello (2026-09-08): `VPTests --level`.**
  Kit sintetico normalizzato a picco 0.9, poi 0/-6/-12/-18 dB e caso clippato,
  su 52/91/168 BPM; 38 s a corsa con un vuoto di 2 s a 26 s. Cinque numeri
  separati — ottava (`|log2| < 0.25` tenuta due beat), ingresso, `FISSO`, fase
  segnata meno l'anticipo, rientro — più `bpm@4s` per sapere *quale* livello
  sbagliato stava leggendo. Non è nella suite completa: 15 corse sono ~4:46.
  `--level 52|91|168` per un tempo solo. Due corse danno numeri identici.

  Esito **13 PASS / 3 FAIL**. 168 BPM è pulito a ogni livello pulito (ottava
  0.29 s, ingresso 0.41 s, fase 6.5 ms). 91 BPM a -12 dB aggancia in 3.35 s,
  dentro le due battute: la correzione qui sopra vista dal banco sintetico.
- [x] **Corretta la pazienza dello snap d'ottava (2026-09-08).** Le due
  concessioni `kOctaveSnapBeatsHealthy` e il bonus di anzianità si applicavano a
  qualsiasi disaccordo oltre 0.25 ottave. Esistono per l'ambiguità **d'ottava**:
  una griglia doppia cade su ogni battito rilevato, quindi sembra sempre sana.
  Un disaccordo lontano da un numero intero di ottave non ha quella scusa —
  67.7 e 90.9 non sono la stessa pulsazione contata in due modi. Ora entrambe
  sono subordinate a `octaveArgument` (`kOctaveArgumentTolerance` 0.15).

  Guadagno molto più largo del caso che l'ha fatta trovare:

  | banco | prima | dopo |
  |---|---|---|
  | `probe_tempo_step` 120→150 | 17.2 s | **12.8** |
  | 100→160 | 13.5 s | **10.1** |
  | 160→100 | 15.0 s | **12.0** |
  | 90→120 | 26.7 s | **20.7** |
  | `probe_matrix` aggancio medio | 5.25 s | **5.20** |
  | `probe_matrix` uscite | 101 | **99** |
  | `VPAlign` gradino 100→140 | 15.14 s | **11.26** |

  `swing 8vi` passa da 1/30 a **0/30** uscite, `swing pieno` da 3/30 a 2/30.
  Nessuna riga peggiora. `--octave` 7/4 identico, `--tempo-slow` 10 PASS.
- [ ] **91 BPM a 0 dB: causa trovata, non corretta.** `tryFastAcquire` pubblica
  **64.55** a 2.58 s mentre lo state space nomina **115.38 con margine -0.174**
  — non incerto, contrario. È la stessa forma del bug del punto 2 ma sotto i
  180 BPM: qui a disattivare il controllo di livello è la regola `rawBpm < 90`,
  che prende per buona la spaziatura osservata per non ripetere 76 → 152.
  Stringerla su una sola riga sintetica è ciò che l'item 23 vieta, e quella
  regola è l'unica cosa che tiene 76 lontano da 152: serve materiale reale a
  più livelli. Con la correzione qui sopra: 17.71 → **15.67 s**, ancora FAIL.
- [x] **Corretto (2026-09-08): il decoder ora tiene il livello sotto una parte
  che suona.** Il caso era 168 clippato — quattordici secondi di griglia sana
  (res 0.014, settled) e poi **il pettine stesso** a 84.03, con il decoder che
  lo segue. Stessa cosa dall'estremo opposto: la registrazione a -18 dB aggancia
  91 e poi va a 182.37 per quattordici secondi. Le sorgenti sono d'accordo fra
  loro sul valore sbagliato: sbagliato è il **momento**, non l'evidenza.

  `updateAutoOctave` rifiutava già di muovere il livello sotto una parte che
  suona, ma poteva farlo solo sul proprio shift: se è il decoder a dimezzare il
  tempo riportato, `autoOctave` resta a zero e la griglia si sposta lo stesso.
  Ora `sounding` arriva al decoder (`BeatTracker` → `NeuralBeatTracker` →
  `BeatDecoder`, stesso passaggio dell'ottava utente) e il voto dello snap si
  ferma su `sounding && !provisional && octaveArgument`.

  | banco | prima | dopo |
  |---|---|---|
  | `VPTests --level` | 13 PASS / 3 FAIL | **15 / 1** |
  | 168 clippato, finale | 84.04 | **168.03** |
  | `VPLive --gain -18`, BPM medio | 121.06 (+33%) | **91.08 (+0.09%)** |

  `--octave` 7/4 identico (÷2 e ×2 compresi), `--tempo-slow` 10 PASS,
  `--state-timing` 3 PASS, `VPAlign` identico. `probe_matrix` e
  `probe_tempo_step` identici **perché pilotano il decoder da soli e non
  chiamano mai `setSounding`**: non possono regredire e non possono validare.
- [ ] Scambio accettato: una **sezione half-time vera** dentro il brano ora
  viene rifiutata come cambio d'ottava. È lo stesso scambio dell'item 17, esteso
  al decoder; ÷2 / ×2 restano la via manuale. Da ascoltare su materiale che ne
  ha una.
- [ ] A -18 dB la deriva media resta **61.6 ms** (max 349.3): il tempo ora è
  giusto per tutta la corsa, ma la fase a quel livello è povera lo stesso. È il
  degrado che la colonna «fase» misura, non l'ottava.
- [x] **`FISSO` non era un terzo difetto** — etichetta mia sbagliata alla prima
  lettura. A 91 BPM: -6, -18 e clip raggiungono `fixed`; 0 dB resta `unknown`
  come conseguenza del punto sopra; -12 dB va in **`live`**, cioè su un tempo
  costante decide che si muove, coerente con i suoi 32.6 ms di fase. `mayFix`
  chiede `lastFitResidual < 0.05` e una finestra assestata: `FISSO` segue la
  qualità dell'aggancio.
- [ ] **La fase peggiora col livello a 91 BPM**: 2.8 ms a 0 dB contro 26-33 ms
  a -6/-12/-18. A 168 BPM non succede. Serve prima la verità di fase annotata
  sulla registrazione per sapere se conta davvero.
- [ ] 52 BPM legge 104 a **ogni** livello: è l'item 1, non il guadagno.
  Misurato e deliberatamente **non** asserito nel filtro.
- [ ] Verita' di fase annotata sulla registrazione (30-120 / 120-210 / 210-250 s)
  prima di credere ai picchi isolati da 85-167 ms.
- [ ] iPad, con variazione del gain hardware.

Dettaglio completo e comandi in `docs/HANDOFF_TEMPO.md`.

---

### 25. Il volume d'ingresso: fader inguidabile e barra che non dice niente 🟢 (2026-09-08, corretto, visto su Mac e provato su iPad)

Segnalazione dell'utente: *«il volume di ingresso... anche la barra del volume,
perche' e' estremamente sensibile e non si capisce quale potrebbe essere il
volume piu corretto di ingresso»*. Due difetti distinti, con la stessa causa:
nessuno dei due era in dB.

**La barra.** Era `sqrt(picco) * 3.2`, che si riempie a **-20 dBFS**: qualunque
livello che un palco produce davvero la mandava a fondo scala, quindi l'unica
cosa che sapeva dire era «sta arrivando qualcosa». Ora e' una scala in dB su 48,
con una **striscia chiara** disegnata dove l'analisi vuole stare, presa dalle
misure dell'item 24:

- sotto circa **-18 dBFS di picco** la rete e' fuori dal livello su cui e' stata
  addestrata e il tempo se ne va con lei (la registrazione reale a quel livello
  passava dieci secondi su un'ottava alta falsa);
- sopra circa **-1 dBFS** la guardia di clipping dell'analisi
  (`kMakeupClipGuardPeak`, item 16) inizia a riportare giu' il segnale, quindi
  alzare ancora non compra piu' niente.

Banda a `kInputLowPeak` 0.2512 (-12 dBFS) .. `kInputHighPeak` 0.8913 (-1 dBFS),
cioe' dal 75.0% al 97.9% della larghezza. Riempimento grigio sotto, fuchsia
dentro, ambra sopra, con due tacche sui bordi. Tenuta di picco con rilascio
lento (salita immediata, ~20 dB al secondo in discesa): a 15 fps il picco grezzo
di una band sfarfalla di 20 dB fra un colpo e il vuoto dopo, e una barra che
sfarfalla non si legge contro una banda.

La frase sotto la barra diceva «SENTO LA STANZA», che rispondeva a un'altra
domanda. Ora dice **IN ASCOLTO / MIC BASSO, ALZA / LIVELLO OK / MIC ALTO,
ABBASSA**.

**Il fader MIC.** Era lineare in ampiezza su 0..2 con 180 punti di trascinamento:
un punto di dito valeva 0.10 dB all'unita', **0.92 dB a -20 dBFS e 2.13 dB a
-28** — cioe' la parte piu' nervosa del controllo era proprio quella dove uno
sta cercando, perche' e' li' che il livello e' troppo basso. Ora:

| | prima | dopo |
|---|---|---|
| corsa | 180 punti | 600 |
| scala | lineare 0..2 (+6 dB max) | unita' a meta' corsa, 0..4 (+12 dB max) |
| lettura | `100%` | `+0.0 dB` |
| dB per punto a 0 dB | 0.10 | **0.06** |
| dB per punto a -20 dBFS | 0.92 | **0.18** |
| dB per punto a -28 dBFS | 2.13 | **0.28** |

Da 1.6 a 7.6 volte piu' fermo, e piu' fermo dove prima era peggio. Doppio tocco
riporta a unita' come prima. Il tetto a +12 dB serve perche' una mandata a
-30 dBFS non arrivava alla banda con +6. Il motore gia' limita a 4.

Aggiunta una riga alla nota INPUT delle impostazioni: alzare finche' la barra
entra nella striscia.

- [x] Codice: `Source/UI/MainComponent.cpp` (`meterPosition`, `micGainText`,
  `kInputLowPeak`/`kInputHighPeak`, `micHold`), `MainComponent.h`.
- [x] Mappatura verificata numericamente (tabella qui sopra). Compila.
- [x] **Visto sul Mac (2026-09-08)**, tema chiaro e scuro, con il brano
  dell'utente caricato in BRANO: manopola `0.0 dB` con lancetta a ore 12,
  striscia e tacche leggibili su entrambi i fondi, barra al 62% con verdetto
  `MIC BASSO, ALZA` (il file entra a circa -18 dBFS di picco). I tre stati e i
  tre colori verificati dall'utente muovendo la manopola.
- [x] Due difetti trovati **guardandolo** e corretti subito: il gradiente
  finiva in `text()`, bianco in tema scuro, quindi la punta della barra era
  bianca in tutti e tre gli stati e il colore sopravviveva solo a sinistra (ora
  il colore sta sulla punta); e in BRANO il verdetto non compariva affatto,
  perche' la riga scriveva `BRANO DIRETTO`, che ripeteva l'etichetta accanto.
- [x] **iPad provato (2026-09-08)**: l'utente riferisce «sembra ok». È un
  giudizio a orecchio e a occhio, non una misura: non sono stati registrati
  livello, tempo alla prima ottava corretta né ingresso. Se serve un numero da
  quella prova, va rifatta registrando la mandata.
- [ ] Resta da vedere su una mandata con **gain hardware che cambia in corsa**
  e con il ritorno acustico delle percussioni accese: il banco host non simula
  microfono, stanza, latenza I/O né feedback degli strumenti.
- [ ] La banda e' in **picco**. Sulla registrazione reale il picco naturale era
  -4 dBFS con RMS -19.5 (fattore di cresta 15 dB); un segnale molto compresso
  entrera' in banda con un RMS piu' alto. Se sul palco la striscia risulta
  ottimista, la soglia giusta e' l'RMS, non il picco.

---

### 26. `VPTests --bar` falliva da settimane: era il test 🟢 (2026-09-08, corretto il test, codice invariato)

«within two bars of the return the one is beat zero again» falliva identico a
`e5cc06e`, `11618cb`, `aa97ced`, `9fcaf02` e `dcff368` — ricostruiti uno per uno.

`barPhase * 4` è la posizione nella battuta **all'istante in cui si guarda**, e
lo stub mette l'uno dove `(beatNo + 2) % 4 == 0`. Lo zero era vero finché la
finestra di recupero era `fourBarsSec + 1.0` (27.797 s → battito 46, e 46+2 è
multiplo di 4); il commit `74fe5fc` l'ha stretta a `twoBarsSec` senza
ricontrollare dove si cadeva (22.0 s → battito 36, quindi 2).

Con la finestra resa variabile, il conteggio ha coinciso con `(beatNo + 2) % 4`
a **sei lunghezze su sei** da 4.8 a 10.6 s. Il test ora calcola l'atteso come fa
già il gemello `bar-seek`. **10 PASS / 0 FAIL**, tre corse identiche, nessun
codice di produzione toccato.

**L'1 che sembrava non allinearsi da solo: si allinea, dopo dodici battute.**
Prima del buco il conteggio legge 3 dove l'uno del modello è su 2, con `rot=0`.
Avevo scritto che non si allineava mai: è sbagliato. Tracciando `tryAlignFrom`
l'opinione è schiacciante subito (`best=1 bestV=0.92 runner=0.03`) e a fermarla
è solo `voteBeats`, che non arriva a `kBeatsToMoveTheBar`. Spostando il buco, la
rotazione cade fra i **46.7 e i 53.3 battiti**, che è esattamente il conto:
`voteBeats = voteBeats * 0.982 + 1` converge a 55.6 e attraversa 32 al
**quarantasettesimo battito** — dodici battute, 29 s a 100 BPM, 59 s a 50.

Non è un difetto: è il prezzo voluto di spostare una battuta che si sente.
Mancava solo che il numero fosse scritto — «32» non dice «dodici battute». Ora è
annotato accanto alla costante, con la misura, e il test stampa anche dove l'uno
**è** prima del buco.

- [ ] **Decisione tua, non un bug**: se l'app entra sull'1 sbagliato ci mette
  mezzo minuto a correggersi da sola (quasi un minuto a 50 BPM). «SPOSTA L'1» è
  lì per quello. Se mezzo minuto è troppo, il numero da muovere è
  `kBeatsToMoveTheBar` — ma abbassarlo rende la battuta più mobile sotto le mani
  di chi suona, che è il motivo per cui è alto.

---

### 27. Gradini piccoli di tempo: seguire un cambio di un paio di BPM 🟡 (2026-09-08, misurato e migliorato — resta l'ascolto)

Richiesta: *«far seguire e tenere ancora meglio qualsiasi tempo, anche con un
cambio di un paio di BPM improvviso»*, entro circa due quarti, con rientro
immediato dalla deriva.

Il gate di astra (regime `fixed`, feed diretto, due fit di quattro battiti che
devono essere puliti e consecutivi) riscriveva `bpm` e basta: il clock ci
arrivava con la sua costante di tempo, quindi un gradino già dimostrato atterrava
come una pendenza. Ora pubblica una transizione `rapid` confermata, la stessa dei
salti grandi. Banco: `scripts/probe_small_steps.cpp` (**da aggiungere a git**).

`probe_small_steps` da **6 PASS / 5 FAIL a 8 PASS / 3 FAIL**; 168→170 da FAIL
2.977 s a PASS 1.077 s, 52→54 da FAIL 5.975 s a PASS 4.395 s. Il gate scatta
**3.1 battiti dopo il cambio** a 52, 120 e 168, in entrambi i sensi: è il minimo
teorico, con un intervallo solo non si può sapere che il tempo è cambiato.

`probe_matrix` completo (360 corse) identico byte per byte, `--steady` identico,
`probe_tempo_step` identico, `VPAlign` identico, `probe_recovery` PASS. Il banco
usa il feed diretto e il gate non è **mai** scattato: distingue un gradino da una
deriva musicale di 3 BPM al minuto.

**Ipotesi sbagliata, misurata e scartata:** `beginTempoTransition` azzera sempre
il recupero di fase; sembrava la causa del ritardo residuo. Reso condizionato ai
3 BPM che quella funzione già usa per il trim: **numeri identici riga per riga**.
Ripristinato, niente rimasto in albero.

**Dove vanno i secondi** (52→50): il clock resta sul vecchio tempo per 3.6 s e
l'errore cresce fino a 137 ms; il gate scatta, il tempo è giusto, e la fase
rientra del ~79% per battito fino a fermarsi su 11.4 ms. A 168→170 lo stesso
gradino accumula solo 12.6 ms e non esce mai dai 25 ms. **Non è una costante da
stringere, è geometria**: la finestra di rilevamento è 3.1 *battiti*, quindi
l'errore in millisecondi cresce col quadrato della durata del battito.

- [x] A 120 e 168 BPM il rientro è già immediato (1.1-1.5 s, fase sempre sotto
  i 25 ms).
- [ ] A 52 BPM servono ~5 battiti in tutto, tre dei quali sono il minimo
  teorico. Il criterio di PASS del banco (stabile entro **4** battiti dal
  cambio) è sotto il pavimento: da riscrivere in battiti-dopo-il-rilevamento,
  oppure accettare i due FAIL a ±2 BPM per quello che sono.
- [ ] I salti da ±12 BPM restano lenti (9.3 s a 132, 108 non si stabilizza):
  è il percorso della transizione ordinaria, non questo gate.
- [ ] Il gate è **solo su feed diretto**. Su microfono non è mai stato provato e
  non va abilitato senza una misura sua.
- [ ] Ascolto: un brano che cambia di due BPM a metà, a tempo lento.

---

### 28. I due suoni che non erano quello strumento: clap e cembalo 🟡 (2026-09-08/09, corretti e misurati — resta l'ascolto)

Due richieste dell'utente, stessa forma di difetto: una voce trattata come se
fosse un altro strumento.

**Il clap** (2026-09-08, già committato). Era il campione VCSL *Claps*: una
registrazione buona e lo strumento sbagliato, una stanza di gente che batte le
mani **una volta**. Centrato a 3953 Hz con il 65% dell'energia fra 2 e 6 kHz e
il 20% nel corpo — un «tss», e perfettamente mono. Ora `synthesizeClap` (i tre
`Assets/Percussion/clap*.wav` non vengono più letti, `kStem` è `nullptr`):

| | vecchio | nuovo |
|---|---|---|
| colpi | 8, 12, 22, 26 ms | 2, 8, 12, **20** ms |
| coda a -30 dB | 86 ms | **186 ms** |
| centroide | 3953 Hz | **2293 Hz** |
| energia 0.5-2 kHz | 19.9% | **74.1%** |
| correlazione L/R | +1.00 (mono) | **+0.65** |

Quattro mani dentro 20 ms con spaziatura che si stringe con la forza e jitter
per round-robin; corpo su due passabanda a 920 e 1260 Hz su rumori indipendenti
(le mani a coppa sono un risuonatore di Helmholtz); 2 ms di schiocco a 3.3 kHz
sopra ogni colpo; coda decorrelata fra i canali. Compensazione d'attacco 15.58 ms
contro i 10.12 del campione: il colpo udibile resta sul beat.

**Il cembalo** (2026-09-09). Qui il campione era **già vero** — tamburello VCSL
— ma `layerFromRecording` rilegge ogni registrazione a `kDrumTune` (2^(10/12) =
1.782) escludendo solo lo shaker. Quella costante è una decisione **sulle
congas** e lo dice dove è definita. Un tamburello non ha una pelle da accordare.

| | prima | dopo |
|---|---|---|
| parziali dominanti | 11.84 kHz | **6.64 kHz** |
| centroide | 14299 Hz | **9719 Hz** |
| energia 4-10 kHz | 7.5% | **62.1%** |
| energia oltre 16 kHz | 24.8% | **3.6%** |
| coda a -30 dB | 35 ms | **60 ms** |

Nei file sorgente i sonagli stanno a 6.3 kHz, quattro quinti dell'energia fra 4
e 10 kHz e **zero** sotto il kilohertz; a ×1.782 finivano a 11.2 kHz, un quarto
sopra i 16 kHz dove quasi nessuno sente, e i 200 ms di risonanza diventavano 112.

Correzione in due righe, nessuna modifica logica: il cembalo escluso da
`kDrumTune` come lo shaker, e stessa banda dello shaker (3-12 kHz invece di
1.6-6.8) per l'attenuazione dei colpi piani — un polo a 1600 Hz su uno strumento
senza corpo non toglie il vertice del colpo, toglie il colpo. L'argomento «è un
numero solo, così le due metà del banco restano accordate» non si applica:
`synthesizeCymbal` è scritta in hertz assoluti e non ha mai usato `kDrumTune`.
Attacco da 1.12 a 1.96 ms (down) e da 2.79 a 5.38 ms (up), sempre molto sotto il
clap che fissa l'anticipo globale: il cembalo cade dove cadeva.

- [ ] **Ascolto di entrambi in contesto**, con congas e shaker, sul brano vero.
  Le misure dicono che ora sono lo strumento giusto; non dicono se stanno bene
  nel mix.
- [ ] I tre `clap*.wav` sono ancora sul disco e nel binario (~90 KB): se il clap
  sintetico convince, si cancellano e si tolgono le righe da `ATTRIBUTION.md`.
- [ ] Nessun test copre il timbro delle voci. Il solo controllo automatico è la
  regressione d'attacco; il resto è `VPRender` più l'orecchio.

**Terzo caso, stessa forma (2026-09-09).** Segnalazione: *«sembra che sul 2 e sul
4 stia suonando un colpo differente»*. Era vero, ed era il **livello dinamico**.

`pick()` sceglie un layer con `int(velocity * 3)`, e per il cembalo ogni layer è
una *registrazione diversa* — `cembalo_down.wav` contro `cembalo_down_med.wav` —
più un filtro diverso in `layerFromRecording` (taglio a 12 kHz contro 7.5). La
velocity del cembalo è `shaker[step] * accent[beat]`, e attraversa il confine a
0.667 fra un quarto e l'altro:

| stile | beat 1 | beat 2 | beat 3 | beat 4 |
|---|---|---|---|---|
| marcha | 0.900 **L2** | 0.636 **L1** | 0.800 **L2** | 0.616 **L1** |
| dance | 0.700 **L2** | 0.551 **L1** | 0.698 **L2** | 0.513 **L1** |
| funk | 0.880 **L2** | 0.644 **L1** | 0.768 **L2** | 0.602 **L1** |
| samba | 0.648 L1 | 0.940 L2 | 0.598 L1 | 0.900 L2 |
| pop / bossa | L2 | L1 | L1 | L1 |
| rock | L2 | L2 | L2 | L2 (l'unico sano) |

Due timbri, agganciati al backbeat. E con `humanize` a ±7% i valori vicini al
confine (marcha 0.636, funk 0.644, samba 0.648) saltavano livello da un colpo
all'altro, quindi non erano nemmeno due in modo coerente.

**Correzione**: il cembalo non ha layer dinamici. Non è pigrizia sulle sue
dinamiche, è cosa è lo strumento — una pelle di conga colpita piano perde il
crack e risuona meno, un tamburello colpito piano è le stesse sonagliere che si
muovono meno, e l'unica differenza onesta è il livello. Una sola presa
(`cembalo_down.wav` / `cembalo_up.wav`) a forza piena per ogni layer e ogni
round-robin; gli accenti restano, perché `pick` copre già 0.08-1.00 di guadagno
in continuo con la velocity.

Questo chiude anche la stessa cosa che arrivava per un'altra strada: per
`ATTRIBUTION.md`, `cembalo_down_b` e `cembalo_up_med` sono un **secondo
tamburello** (VCSL Tamb1 contro Tamb2), quindi il round-robin cambiava strumento.
I file restano sul disco, semplicemente non vengono più letti per questa voce.

Misurato con `VPRender` (marcha, 100 BPM, `--humanize 0`, solo cembalo),
centroide spettrale dei colpi sugli ottavi:

```
          1      e      2      e      3      e      4      e
prima   9438   9710   9512   9704   9430   9711   9512   9851
dopo    9438   9877   9439   9887   9439   9879   9439   9891
```

Prima l'1 e il 3 leggevano ~9434 Hz e il 2 e il 4 leggevano 9512: campione
diverso. Dopo tutti i down leggono 9439 e tutti gli up ~9880 — restano due
timbri, che è giusto, perché down e up sono due articolazioni. Su rock, dance,
funk, samba e pop l'escursione fra i down è **0 Hz** e fra gli up ≤10 Hz. I
picchi (gli accenti) sono invariati riga per riga. `--swing` 3/0, `--bar` 10/0.

- [ ] **Da ascoltare.** La misura dice che i colpi sono lo stesso suono; non dice
  se il cembalo senza layer dinamici suona piatto in un crescendo vero. Se lo è,
  la strada non è rimettere i layer ma dare a `layerFromRecording` una curva di
  brillantezza continua per il metallo, invece di tre gradini.
- [ ] Lo **shaker** ha la stessa struttura (stesse tabelle, stessi accenti,
  stessi tre layer da tre file). Non è stato segnalato e non è stato toccato:
  va misurato allo stesso modo prima di decidere.

**Shaker e congas, ripetizione residua (2026-09-11).** Il round-robin dichiarava
tre celle, ma alle velocity medie usate normalmente dal groove tutte e tre
leggevano la stessa unica presa. Lo stesso valeva inevitabilmente per
heel/toe/muff e per i colpi stoppati derivati da open/slap. Il test sul layer
forte non lo vedeva perché lì esiste una presa B.

- [x] `layerFromRecording` costruisce ora tre impronte molto piccole di colore
  e sustain, deterministiche per cella. Simulano la mano che cade in un punto
  leggermente diverso della pelle e i grani dello shaker che si dispongono
  diversamente; non spostano l'inizio e non cambiano playback-rate/accordatura.
  È tutto fatto in `prepare()`, quindi sul thread audio non è comparso lavoro.
- [x] Regressione mirata `VPTests --percussion`: differenza relativa fra due
  colpi medi consecutivi **0 → 0.000470 shaker**, **0 → 0.000171 open conga**;
  1/1 PASS. `VPTiming --attacks`: contatto udibile resta nel banco compensato
  (shaker down 13.04 ms al 50%; tumba 15.42; open 14.60; slap 15.38).
- [x] A/B con stesso seed, pattern e mix: marcha 112 e dance 124, otto battute.
- [ ] **Ascolto umano dell'A/B.** La misura prova che non sono più buffer
  identici e che non si è mosso il colpo; la scelta di quanto sia più bella la
  può chiudere soltanto chi la sente nel mix.

### 28b. Le congas suonavano finte: accordatura e quinto stoppato ✅ (2026-09-14, misurato — resta l'ascolto)

Segnalazione: *«le percussioni sono molto finte e hanno un suono brutto»*, poi
*«le conga sono troppo basse di tonalità»*, poi la forma finale: *«la conga più
alta la vorrei proprio stoppatissima, più africana; le congas con il classico
suono pop»*.

**Fix.** (1) `kDrumTune = 2^(5/12) = 1.335` (una quarta sopra il naturale, dopo
aver tolto la settima maggiore `2^(10/12)` che rendeva la tumba un tom): tumba
185 Hz, open 220 Hz, slap 288 Hz. (2) Solo il **quinto** è stoppato:
`PercussionEngine::trigger` passa per `stoppedConga`, che mappa `slap`→
`slapClosed` (il crack africano senza coda) e lascia tumba/open a suonare il
tono aperto del pop classico. La coda dei colpi registrati è limitata a 0.30 s.
Attacco sentito +0.55 ms, `--percussion` 1/0, `--swing` 3/0, `--bar` 10/0.

**Costo dichiarato.** Sul percorso speaker il canceller rimuove ~11% del rientro
delle congas (contro 25.2% con le open a 1.782); sul percorso mixer/file — quello
richiesto — il ritardo è noto e il fit a tre bande si adatta da sé. Da risolvere
solo se il microfono iPad torna prioritario.


---

### 29. Su un intro senza batteria l'app si impegna su un tempo sbagliato e ci resta un minuto 🔴 (2026-09-09, misurato su brano reale — causa trovata, non corretta)

Segnalazione: *«spesso non riconosce il bpm del brano»*, *«va fuori e rientra
subito appena muovo leggermente il knob del mic»*, *«passa da 87 a 65 senza
senso»*. Brano di riferimento: `01 BLUE SKY.mp3` sul Desktop dell'utente,
convertito in `/tmp/vp-bluesky.wav` (l'originale non è toccato).

**Banco nuovo: `VPTrack`** (`scripts/probe_track.cpp`). Legge un wav e lo manda
nel motore **completo** — trim, canceller, guadagno d'analisi e
`updateAnalysisEpoch` — invece che nel solo `BeatTracker` come fa `VPLive`.
Serviva: su questo brano i due percorsi danno risposte diverse, e quello vero è
il peggiore.

```bash
cmake --build build-host --target VPTrack -j4
./build-host/VPTrack_artefacts/Release/VPTrack --wav /tmp/vp-bluesky.wav --bpm 87 --trace
```

**Il brano.** I primi trenta secondi non hanno né batteria né basso: energia
sotto i 200 Hz da 5.7 a 9.9, che sale a 37 e poi 79.7 fra i 30 e i 40 s quando
entra la sezione ritmica. Picco d'ingresso nell'intro: 0.013-0.020.

**Cosa fa il motore completo, a livello invariato:**

```
  t     pubbl   pettine  conf  gAnalisi
  4.0   52.49     0.00   0.52    7.54
 16.0   57.81   173.41   0.83    3.32
 20.0  171.29   181.82   0.10    3.40
 28.0  118.12   103.45   0.53    4.17
 40.0  163.01   170.70   0.68    1.92
 53.2  <- qui scatta il restart dell'analisi
 68.0   86.97    87.08   0.61    1.00
 88.0   86.38    86.58   1.00    1.00   FISSO
```

Primo aggancio tenuto 3 s: **66.1 s**. Dentro il ±2% solo il 45.6% dei primi
due minuti.

**Prima ipotesi, sbagliata e corretta subito.** Il guadagno d'analisi nei primi
50 secondi:

| livello | gAnalisi nei primi 50 s | primo aggancio | dentro il 2% |
|---|---|---|---|
| **0 dB** | 7.56 → 7.81 → 4.33 → 3.40 → 6.46 → 3.21 → 4.95 → 1.92 → 1.29 | **66.1 s** | 45.6% |
| −6 dB | 15.08 → … → 2.57 (saturato in alto) | **8.5 s** | 69.8% |
| +12 dB | 1.90 → 1.96 → 1.09 → 1.00 → 1.62 → 1.00 → 1.24 → 1.00 | **12.1 s** | 59.5% |

Sembrava la causa. **Non lo è**, e le due misure che l'hanno smontata:

1. A 0 dB e a −6 dB il livello **dopo** il guadagno è identico a tre decimali in
   ogni istante campionato (0.120 / 0.100 / 0.161 / 0.190 / …): il make-up
   compensa il trim esattamente come deve. Stesso segnale alla rete, e i due
   agganciano a 66.1 s e 8.5 s.
2. Sweep del tetto del guadagno, a livello invariato:

   | tetto | primo aggancio |
   |---|---|
   | 24× (attuale) | **66.1 s** |
   | 12× | 8.2 s |
   | 6× | 17.8 s |
   | **3×** | **66.1 s** — identico a 24×, a sei decimali |
   | 2× | 25.3 s |

   Nessuna monotonia. Non è un livello di soglia: è l'esito che cade in uno di
   pochi bacini, e 66.1 s è quello in cui finiscono impostazioni lontanissime.

**Quello che succede davvero.** Quattro corse identiche danno lo stesso numero a
sei decimali: il banco è deterministico. Ma la mappa fra configurazione ed esito
non lo è in alcun modo utile — **qualunque** perturbazione della catena d'analisi
fa ricadere il brano su un bacino diverso. Il knob non «sistema» niente: rimescola.

E il motivo per cui esistono bacini così diversi è a monte di tutto: **nei primi
trenta secondi non c'è una sezione ritmica**, la rete produce risposte
*confidenti e sbagliate* — 52, 57, 171, 120, 163, 133 — e l'app **si impegna su
una di quelle a 4 secondi**, con confidenza 0.52, e suona. Da lì in poi il minuto
successivo è deciso da quale sbagliata le è capitata.

**Il cancello: provato a costruirlo, e la misura dice dove non può stare.**

L'idea era: non impegnarsi finché non c'è pulsazione, usando la banda bassa come
segnale che una sezione ritmica non c'è. `lowBand` arriva già al decoder a ogni
frame (`observe(..., lowBand)`), quindi era costruibile. Misurato sul brano:

```
LB t=  4.02  medio=0.3818   <- intro senza batteria
LB t= 12.06  medio=0.6547
LB t= 20.10  medio=0.5177
LB t= 40.16  medio=0.7782   <- band entrata
LB t= 72.20  medio=0.7331
LB t= 96.32  medio=0.7879
```

**Non separa niente**: 0.65-0.70 nell'intro contro 0.73-0.79 dopo. Il motivo è
strutturale e sta scritto in `updateAnalysisEpoch`: la banda bassa è misurata
**dopo** il guadagno d'analisi, che nell'intro amplifica 7.5×, e a valle di quel
guadagno «an empty room and a band playing arrive looking alike - by design».

Quindi un cancello basato su qualunque cosa misurata dopo il make-up **non può
vedere** che manca la sezione ritmica. Deve stare prima, cioè dove vive già
`updateAnalysisEpoch` — l'unico punto in cui la differenza esiste ancora.

- [x] **Il cancello va costruito nel motore, prima del guadagno d'analisi**, non
  nel decoder. Lì `rawPeak` e la banda bassa del segnale non amplificato dicono
  ancora se c'è una sezione ritmica. È lo stesso posto che già decide l'epoch, e
  probabilmente le due cose sono lo stesso lavoro.
- [ ] Un cancello basato solo sulla **stabilità del tempo pubblicato** non basta,
  e la traccia lo dimostra: il decoder resta fermo su 118 BPM per otto secondi
  (t=28-36) e su 133 per quattro. È confidente, stabile e sbagliato.
- [x] L'epoch scatta a **53.2 s**, venti secondi dopo l'ingresso della band
  (30-40 s), perché il riferimento era già stato trascinato in alto dall'intro
  amplificata. Da rivedere insieme al punto sopra.
- [ ] **Non toccare il guadagno d'analisi** sulla base di questo brano: è
  misurato che non è la causa, e le sue costanti sono tarate altrove.

**COSTRUITO (2026-09-09): la quota di banda bassa, prima del make-up.**

`VirtualPercussionEngine::updateRhythmShare` misura, sul segnale d'analisi non
amplificato, quanta dell'energia sta sotto i 200 Hz — un **rapporto**, non un
livello. È l'unica statistica misurata su questo brano che separa l'intro dalla
band, e sopravvive al make-up perché un guadagno a banda larga non può falsare
un rapporto. Numeri sul brano, dal motore stesso (colonna `lowS` di `VPTrack`):
intro 0.10-0.28, band 0.32-0.50.

Due fatti diversi ne escono, e servono a due cose diverse:

- **Il gradino** (share > 2× l'altopiano in cui stava, tenuto 1.5 s, sopra 0.30):
  «è appena entrata una sezione ritmica». Fa scattare l'epoch, con la stessa
  uscita del gradino di livello. Sul brano scatta a **39.7 s** invece di 53.2.
- **`rhythmSeen`** (share sopra 0.30 per 2 s, o il gradino): «adesso ce n'è una».
  Ora è richiesto, insieme al livello, da `tracker.setSourceAudible` — cioè
  dall'unico percorso che lascia entrare la parte su musica già in corso
  (`alreadyPlaying`). Una sorgente che parte dal silenzio entra dal suo epoch e
  non è toccata da questo.

| | prima | dopo |
|---|---|---|
| primo aggancio tenuto 3 s | 66.1 s | **55.0 s** |
| dentro il ±2% (brano intero) | 67.5% | **71.8%** |
| epoch | 53.2 s | **39.7 s** |
| suona durante l'intro | sì, da 4.0 s a 52 BPM | **no, mai** |

È relativo per costruzione, ed è questo che lo rende sicuro altrove: il kit
sintetico dei banchi sta piatto a 0.17 per tutta la sua durata e il click a
0.002, quindi nessuno dei due può gradinare, e tutti e due entrano dal loro
epoch di livello (`quietLeadIn`).

**La costante che è costata la prima versione.** Il primo taglio del cancello
d'ingresso ha portato `--level` da 15/1 a **13/3** — proprio la colonna
«ingresso». Non era la soglia: era che `rhythmSeen` non poteva essere vero prima
di 4 s (2 s di prime più 2 s di tenuta) mentre sui banchi la parte entra **0.41 s
dopo l'inizio della musica**. Le due energie ora sono innescate al primo blocco
invece di essere rilasciate, e la tenuta è 0.33 s. I numeri sul brano non
cambiano di un decimale e `--level` torna a 15/1. Chi tocca queste costanti
rimisuri quella colonna: è l'unica che le vede.

Regressioni, tutte rimisurate sulla versione finale: `--level` **15/1** (stesso
unico FAIL, 91 BPM a 0 dB), `--octave` **7/4**, `--bar` **10/0**,
`--tempo-slow` **10/0**, `probe_matrix` **identico** (99 uscite, aggancio medio
5.27 s), `probe_tempo_step` invariato, `VPAlign` **identico riga per riga**
contro un binario ricostruito dai sorgenti puliti. `probe_matrix` e
`probe_tempo_step` non compilano nemmeno il motore, quindi non potevano
cambiare; `VPAlign` sì, ed è stato confrontato davvero.

**L'arbitro pettine/rete, nel decoder (stessa data).** Idea dell'utente: «il
tempo fra due colpi». Un arbitro c'era già — lo snap d'ottava e il watchdog
della griglia stantia — ma non poteva parlare in tempo, e la traccia a 1 s dice
esattamente perché:

```
  39.7  epoch: il fold viene azzerato
  40-47 il pettine non è pronto (0.00) — non c'è niente da arbitrare
  47.0  pettine pronto: 89.69, poi 87.72 / 87.85 / 87.98
  49.0  levelSettled diventa 1
  53.0  snap: pubblicato 57.5 -> 87.8
```

`combMayCorrect` aspettava `tempo.levelSettled()`, e **ogni ragione per
aspettarlo è una ragione d'ottava**: senza, il pettine potrebbe star nominando
il doppio. Non dice niente su due letture distanti una quinta (55.9 contro
87.7) — su qualunque ottava sia il pettine, la griglia impegnata non è su
nessuna delle due. Quindi un disaccordo **non d'ottava**, con pettine saliente,
non aspetta più. Snap a 49.0 invece che 53.0.

Solo contro un livello **provvisorio**, e quel vincolo è misurato, non
supposto. Senza, la stessa rilassatezza arriva a una griglia stabilita e
`VPAlign` dice quanto costa: 132 BPM con 2.2 ms di jitter passa da rms 0.07
battiti a 0.23 con un peggiore di mezzo battito — il livello sbagliato, preso
con l'argomento che questo doveva risolvere — e la rampa 100→110 in 12 s perde
5 ms di fase del decoder. Con il vincolo, entrambe tornano identiche alla base.

| | senza arbitro | con arbitro |
|---|---|---|
| primo aggancio tenuto 3 s | 55.0 s | **53.5 s** |
| dentro il ±2% | 71.8% | **73.6%** |
| snap dopo il restart | 53.0 s | **49.0 s** |
| `probe_matrix` uscite | 99 | **96** |
| `probe_matrix` aggancio medio | 5.27 s | **5.25 s** |
| `probe_matrix` fuori medio | 9.00% | **8.99%** |

`probe_matrix` **non è identico**, ed è la prima volta che questo cambio lo
tocca davvero: i tre totali migliorano tutti, ma per materiale il quadro è
misto — `swing 8vi` 3.91 → 3.67 e peggiore 11.60 → 9.56, `mix che ingoia`
8.62 → 8.04 con uscite 16 → 14, contro `swing pieno` 4.31 → 4.83 (ma uscite
2 → 1 ed errore 0.07% → 0.01%) e `solo accordi` 4.86 → 4.90. `probe_tempo_step`
identico. `VPAlign` differisce ancora su due righe: il 168 a 2.2 di jitter, che
è **già annotato «livello sbagliato» nella base** (rms 0.2691 → 0.2730, scatti
8 → 12, ma peggiore 0.4402 → 0.3732), e frazioni di millisecondo sulla rampa
100→110 in 30 s. `--level` 15/1, `--octave` 7/4, `--bar` 10/0, `--tempo-slow`
10/0.

- [ ] **Continuità dell'analisi all'ingresso della band (prova 09/09).**
  Distinto l'evento di quota bassa dal normale quiet-to-loud: il primo conserva
  pettine e stato ricorrente della rete, azzerando la griglia del decoder;
  il secondo continua a scartare tutta l'evidenza. Evento e tipo viaggiano
  insieme nello stesso valore atomico verso il worker.
  Su BLUE SKY, a 0 dB: primo aggancio tenuto tre secondi **53.46 -> 41.285805 s**,
  quota entro ±2% sul brano intero **73.6 -> 73.8%**. Epoch sempre 39.7 s.
  Conservare il solo pettine, resettando la rete, peggiora a **56.971610 s**:
  non basta evitare il riempimento del buffer. La continuità della rete conta.
  Il miglioramento non equivale a stabilità perfetta: fra 47 e 55 s la variante
  continua sale intorno a 90 BPM; inoltre suona già a 40 s mentre il clock
  sta raggiungendo il nuovo tempo. Questi due limiti restano aperti.
  `probe_input_continuity` verifica conservazione e successivo reset completo
  a 52/87/120/168 BPM (8 controlli). Non misura il modello o il suono.

- [ ] **Diagnosi precedente dei 7 s di riempimento**: fra l'epoch (39.7) e il
  momento in cui il pettine è pronto (47.0) non esiste un secondo parere. È il
  riscaldamento del fold dopo che `notifyInputRestart` lo ha azzerato. Da notare
  che a 39.0 s, *prima* del restart, il pettine leggeva già **85.71** con
  `levelSettled` a 1: l'epoch butta via una risposta che era già giusta. Un
  restart che distingua «stanza → band» (dove l'evidenza è davvero della stanza)
  da «è entrata la sezione ritmica» (dove gli ultimi secondi contengono già la
  band) recupererebbe quei 7 s. È il pezzo più grosso che resta su questo brano.
- [ ] Lo stato pubblicato resta `following` durante l'intro (su un BPM sbagliato)
  anche se la parte tace. Il display mente ancora; solo il suono no.

**Provato e scartato (misurato due volte, tenuto in scratch):** un watchdog che
si accorge quando la griglia «muore di fame» — accetta meno del 55% dei battiti
che il suo stesso tempo prevede mentre gli eventi continuano ad arrivare. Scatta
correttamente e porta `90 → 120` da 20.7 a 10.2 s sul banco sintetico, ma sul
brano vero scambia il crollo a 53 BPM con uno a 131 e arriva **allo stesso
secondo**. La variante che adotta il pettine invece di ripartire non scatta, e
giustamente: nei primi 25 s di questo brano il pettine salta 84 → 173 → 87 →
110 → 0, non c'è un secondo parere da prendere.

---

### 30. A volte all'avvio non si sente niente, e serve cambiare il clock e rimetterlo 🟡 (2026-09-09, causa trovata nel codice — non riprodotta a mano)

Segnalazione: *«a volte l'audio non si sente e devo cambiare da "auto" a 44.100
per esempio, e poi rimettere "auto"»*.

**Cosa fa davvero quel gesto.** Su desktop `deviceSampleRate()` in AUTO
restituisce `vp::sessionSampleRate()`, che fuori da iOS è 0, e uno 0 viaggia
fino in fondo come «non scrivere nessuna frequenza». Quindi AUTO e 44100 non
sono due frequenze diverse: AUTO lascia il setup com'è. L'unica cosa che quel
gesto fa è **riaprire il dispositivo**. La cura non è la frequenza, è la
riapertura — ed è questa l'informazione che indica dove guardare.

**Il buco.** Il watchdog del timer aveva questo ramo:

```cpp
else if (haveDevice && ! audioReady)
{
    // Between close and prepareToPlay. Not a stall.
    stalledTicks = 0;
}
```

Senza limite. E quello è uno stato in cui l'app può restare per sempre:
`AudioSourcePlayer` di JUCE chiama `getNextAudioBlock` dal callback del
dispositivo indipendentemente dal fatto che `audioDeviceAboutToStart` sia
passato. Se non è passato, `inputScratch` è ancora il buffer costruito di
default — **zero canali, zero campioni** — quindi in `getNextAudioBlock`
`count`, `nCopy` e `used` sono tutti zero, `engine.process` non scrive niente e
il blocco finisce nel `buffer->clear` in fondo. Silenzio, mentre `audioBlocks`
continua a contare. Il dispositivo sembra vivo, al watchdog viene detto che non
è uno stallo, e non si sente niente finché non lo si riapre a mano.

**Correzione**: lo stesso limite degli altri rami, dodici tick a 15 Hz (~0.8 s).
Una chiusura-e-riapertura vera dura un tick o due, perché l'avvio è sincrono;
oltre il secondo non è una transizione, è un dispositivo che non è mai partito.
Passato il limite l'app fa da sola la riapertura che l'utente faceva a mano, con
lo stesso raffreddamento di 2 s degli altri due casi.

E il motivo dell'ultima ricostruzione ora è **sulla pagina diagnostica** accanto
al contatore: `riavvii 1 device open but never prepared` è una cosa diversa da
`riavvii 1 no audio callback for a second`, e sono tre rig diversi.

- [ ] **Non riprodotto a mano.** È intermittente e dipende dall'ordine di
  apertura del dispositivo; quello che c'è è che questo è l'unico percorso nel
  codice che produce esattamente quel sintomo (dispositivo aperto, callback che
  girano, silenzio, e la riapertura come unica via d'uscita). La verifica è la
  riga `riavvii` sulla pagina diagnostica: se il difetto si ripresenta e adesso
  si risolve da solo dopo un secondo, quella riga lo dirà.
- [ ] Su un'**interruzione iOS** (una telefonata) il dispositivo può restare
  aperto e fermo. Adesso dopo 0.8 s si ricostruisce, cosa che durante la
  chiamata fallisce e riprova ogni 2 s, e al termine rientra da sola. È lo
  stesso comportamento che il ramo «nessun dispositivo» ha già oggi; se in
  pratica dà fastidio, il ramo va sospeso mentre `appIsSuspended` è vero.
- [ ] **AUTO su desktop non è «segui l'hardware», è «lascia com'è».** Fuori da
  iOS `sessionSampleRate()` è 0, quindi dopo aver scelto 44100 e rimesso AUTO il
  dispositivo resta a 44100. Il commento in `deviceSampleRate()` dice perché
  scrivere una frequenza è caro (ri-clocca un'interfaccia che tutta la sala sta
  ascoltando), quindi non è stato toccato — ma l'etichetta mente un po'.

---

### 31. Ogni tanto deriva e ci mette troppo a rientrare 🟡 (2026-09-09, misurato e dimezzato — non ancora dentro la battuta ovunque)

Segnalazione: *«ogni tanto deriva troppo e ci mette tanto a riprendere (anche 5
secondi — deve rientrare in una battuta)»*, con la domanda giusta attaccata:
*«ogni battuta, o ogni tot secondi o millisecondi?»*.

**La risposta alla domanda è la parte più utile di questo item.** Il controllo
esiste già ed è **per battito**. Il problema non è la cadenza: è che due dei
budget sono espressi in battiti, e un battito è un secondo a 60 BPM e 0.375 s a
160. Lo stesso numero è una scadenza diversa a ogni tempo, e la scadenza che
l'ascoltatore sente è la **battuta**.

**Dove vanno i secondi**, `probe_steady_tempo` (tempo costante, deriva 3 BPM,
jitter 10 ms, 11 tempi × 10 semi × 300 s). Sette uscite su 110 corse, e sono due
difetti diversi:

| BPM | battuta | quanto | letto | pettine | fit corto | fit lungo |
|---|---|---|---|---|---|---|
| 60 | 4.00 s | **6.5 / 1.4 / 6.7 s** | 61.08 | 59.70 | 59.67 | 60.87 |
| 150 | 1.60 s | **2.0 s** | 142.90 | 150.38 | **0** | **0** |
| 160 | 1.50 s | **1.9 / 2.2 s** | 152.06 | 159.15 | **0** | **0** |
| 170 | 1.41 s | **1.8 s** | 178.53 | 170.94 | **0** | **0** |

- **Ai tempi veloci** l'uscita segue una transizione che ha **azzerato i fit** e
  pubblicato un numero sbagliato. Il pettine ha ragione per tutta la durata. Il
  numero rientra a 0.7 BPM per battito: `pullTowardsComb` dà al pettine il 35%
  di autorità e `kRateLive` ne applica il 22% per battito — **7.7% effettivo,
  cioè 13 battiti (tre battute) per chiudere**.
- **A 60 BPM** il regime è **FISSO**, e in FISSO il pettine non era consultato
  affatto: `live` e `unknown` passano il loro bersaglio per `pullTowardsComb`,
  quel ramo seguiva `fixedAnchorBpm` e nient'altro. Traccia per battito:

```
  26.76  read=61.08  short=59.48  comb=59.70  FISSO   <- congelato
  27.78  read=61.08  short=59.32  comb=59.46  FISSO
  28.82  read=61.01  short=59.10  comb=59.29  FISSO
  30.86  read=59.84  short=58.72  comb=58.82  LIVE    <- esce solo qui
```

**Due correzioni, entrambe misurate.**

1. **Il pettine entra in FISSO**, limitato bene dentro l'ottava. Non
   `pullTowardsComb` così com'è: il suo tetto è `kOctaveThreshold`, quindi tira
   anche fra l'8 e il 19%, che è dove vive un argomento sul livello metrico e di
   cui lo snap è padrone. Sotto `kStaleGridThreshold` non c'è nessun livello da
   confondere, e il caso a 60 BPM sta al 2-3%. Sotto il 3% non fa niente, quindi
   un disco tagliato a click non viene toccato.
2. **La battuta dopo uno stato stantio si spende a rincorrere**, non a inclinare:
   uscendo da FISSO (il numero tenuto è stale per definizione, è il motivo per
   cui si esce) e mentre `transitionRefitBeats` dice che i fit si stanno
   ricostruendo dopo una transizione. **Non cambia dove va il tempo, solo quanto
   ci mette**: il bersaglio è lo stesso fit ricostruito in entrambi i casi, ed è
   questo che la rende sicura su un gradino vero. Con la guardia del 2%, perché
   senza scattava anche quando il numero era già giusto.

| BPM | battuta | prima | dopo |
|---|---|---|---|
| 60 | 4.00 s | 6.5 / 1.4 / 6.7 | **4.4 / 1.4 / 4.7** |
| 150 | 1.60 s | 2.0 | **0.8** |
| 160 | 1.50 s | 1.9 / 2.2 | **1.1 / 1.8** |
| 170 | 1.41 s | 1.8 | **0.7** |

Dentro la battuta: **1 uscita su 8 prima, 4 su 8 dopo**; la peggiore da 6.7 a
4.7 s. Errore medio del banco migliorato a 8 tempi su 11 e peggiorato a nessuno.

**Tre ipotesi morte alla misura, e vanno lette prima di ritentarle.**

1. **Il pettine come test d'uscita da FISSO.** `anchorError` confronta il fit
   lungo con una media corrente del fit lungo: entrambi i lati vengono dal
   numero che il tempo tenuto produce, quindi un fit lungo in ritardo alimenta
   l'anchor *e* lo certifica. La riparazione ovvia è chiedere al pettine, che è
   fuori da quel ciclo. Funziona sul caso per cui è scritta e **rompe un gradino
   vero**: sul 120 → 160 non protetto di `probe_tempo_step` il pettine nomina
   120 per secondi dopo il cambio, il test legge 25% dalla parte sbagliata, il
   regime viene rilasciato con il pettine stantio come voce più forte, e la
   corsa **collassa a 53.3 BPM e non torna più**, contro 23.6 s per arrivare al
   tempo giusto. Sospenderlo durante una transizione confermata non lo salva: il
   gradino non protetto non ha nessuna transizione da sospendere. È lo stesso
   muro che la skill registra per il gate delle transizioni.
2. **Accorciare il budget d'uscita da FISSO** (`kBeatsToLeaveFixed`, 6 → 4 → 3).
   Durate identiche al decimo di secondo in tutte e tre. L'attesa non è il
   contatore: è il fit lungo che arriva alla linea del 2%, e la sua finestra a
   tempo lento è lunga nove secondi. Ripristinato a 6.
3. **Il fit corto nel test d'uscita.** Era già 2.7% sotto al primo battito. Non
   provato fino in fondo perché la misura che c'è già lo esclude: la skill
   riporta che il movimento battito-per-battito del fit corto è 0.5-1.15 BPM a
   tempo fermo, che a 60 BPM è fino all'1.9% — contro una soglia del 2%.

**Cosa costa, e va deciso dall'utente.** La rincorsa (punto 2) è la metà cara:

- `probe_matrix`: uscite **96 → 98**, tutte e due su `band larga` (banda
  sfilacciata, 25 ms di scatter). In compenso `fuori medio` 8.99% → **8.98%** e
  cinque materiali migliorano la loro percentuale fuori tempo — metronomo
  0.05 → 0.02, backbeat secco 0.42 → 0.30, shuffle 16mi 0.05 → 0.02 — con i
  rapporti più vicini a 1.00. Aggancio medio invariato a 5.25 s.
- `VPAlign`: due righe **migliorano** (a t=32 il tempo passa da 117.91 a 119.32
  contro un vero 120, con la fase da 12.8 a 7.3 ms; `fase media` 14.8 → 14.4) e
  sette righe della media su otto brani **peggiorano di 0.5-3 ms** attraverso il
  buco senza batteria e l'accelerando, su numeri che stanno già a 20-40 ms.
- Invariati: `probe_tempo_step` identico riga per riga, `--level` 15/1,
  `--octave` 6/5, `--bar` 10/0, `--tempo-slow` 10/0, `--swing` 3/0.

Isolato per bisezione: il punto 1 da solo non costa niente (`probe_matrix` 96 e
due righe migliori, `VPAlign` solo le due righe migliori). Tutto il costo è la
rincorsa, ed è la rincorsa a sistemare i tempi veloci. Per toglierla basta
`const bool far = false && ...` in `updateTempo`.

- [ ] **Restano fuori 3 uscite su 8.** A 60 BPM 4.4 e 4.7 s contro 4.0, e a
  160 BPM una da 1.8 s contro 1.50. Quella a 160 è istruttiva: fra due battiti
  accettati passano **1.48 s** — quattro battiti che la griglia rifiuta dopo la
  transizione. Un controllo per battito non può fare niente quando i battiti non
  arrivano, ed è lì che serve la seconda metà della domanda dell'utente: un
  fondo a tempo d'orologio, non a battiti.
- [ ] **A 60 BPM il vincolo è il ritardo del fit lungo**, non il budget. Nove
  secondi di finestra su materiale che deriva, e nessuna delle tre sorgenti
  legge il vero (58.73): il fit lungo dice 60.87, il pettine 59.70, il corto
  59.48. La strada è probabilmente non entrare in FISSO su materiale che deriva,
  cioè `mayFix`, e ha un raggio d'azione molto più largo di questo item.
- [ ] `--octave` è **6/5 su questo HEAD**, non 7/4: è cambiato fuori da questo
  lavoro (le modifiche a `NeuralBeatTracker`/`BeatTracker` di un'altra sessione).
  Ri-baselinare prima di attribuirlo a un cambio del decoder.

---

### 32. «Sempre in ritardo della stessa quantità, e ci mette molti secondi a rientrare» 🟡 (2026-09-10, causa trovata e metà corretta)

Segnalazione, ed è la descrizione che ha risolto il caso: *«sono sempre in
ritardo (o sempre in anticipo) e ci impiegano molti secondi a rientrare»*.

Le due metà di quella frase sono **una sola costante**. `TempoFollower` aveva un
pavimento di fase di `0.012` **di battito**, e una frazione di battito è un
numero di millisecondi diverso a ogni tempo:

| BPM | 52 | 60 | 75 | 90 | 120 | 168 |
|---|---|---|---|---|---|---|
| banda morta | **13.8 ms** | 12.0 | 9.6 | 8.0 | 6.0 | 4.3 |

Il bersaglio dichiarato per dove atterra la parte è **8 ms**, quindi sotto i
90 BPM la banda morta era **più larga della cosa che deve ottenere**. E viene
*sottratta* dall'errore, non confrontata:

```cpp
const float e = phaseErrEma > kPhaseFloor ? phaseErrEma - kPhaseFloor : ...
```

quindi la correzione svanisce mano a mano che l'errore si avvicina al pavimento.
Il residuo non si chiude: **converge al pavimento e resta lì**. «Molti secondi a
rientrare» è l'avvicinamento asintotico; «sempre in ritardo della stessa
quantità» è dove si ferma.

`probe_recovery --slow-passages` lo diceva già, ed era annotato come aperto:
tutti e 18 i casi a 52 BPM leggevano `confirm=-1.000 stable=-1.000` — mai
confermato e mai dentro gli 8 ms.

**Correzione: il pavimento è limitato nel tempo, non in battiti.** Quello da cui
protegge è il rumore di fase dell'*analisi*, che è un tempo e non una frazione
di battito: viene da una griglia di frame da 20 ms, che è 20 ms a ogni tempo.

```cpp
constexpr float kPhaseFloorBeats = 0.012f;
constexpr float kPhaseFloorSeconds = 0.006f;
inline float phaseFloorFor (float periodSec)
{ return std::min (kPhaseFloorBeats, kPhaseFloorSeconds / std::max (0.05f, periodSec)); }
```

6 ms **è** 0.012 di battito esattamente a 120 BPM, cioè dove la costante
originale era stata tarata: quindi **da 120 BPM in su non cambia nulla**, il
valore in battiti è già il più piccolo dei due. Impedisce solo che il pavimento
si allarghi sotto quel tempo.

| | prima | dopo |
|---|---|---|
| `probe_recovery` default (120/168) | 0 FAIL / 84 PASS | **identico** |
| `--slow-passages`, tutti e 18 i casi a 52 BPM | `stable=-1.000` (mai) | **3.3-4.4 s** |
| `VPAlign`, aggancio a 70 BPM | 3.61 s | **3.43 s** |
| `VPAlign`, aggancio a 100 BPM | 3.14 s | **3.05 s** |
| `VPAlign`, dieci righe di rallentando | — | **~0.2 s meglio ognuna** |

Le uniche righe peggiori di `VPAlign` si muovono di **0.1 ms** (39.8 → 39.9), che
è rumore numerico. `--level` 15/1, `--octave` 6/5, `--bar` 10/0, `--tempo-slow`
10/0, `--swing` 3/0, tutte alla base. `probe_matrix` e `probe_tempo_step` non
compilano `TempoFollower` e non possono cambiare.

**Provato e scartato nella stessa ora.** Dare lo stesso trattamento all'altro
pavimento, `persistentFloor = max(0.04f, 0.020f/period)` — che a 52 BPM vale
46 ms — scalandolo con il pavimento tempo-consapevole. Aritmeticamente è lo
stesso numero a 120 BPM, ma **`probe_recovery` è passato da 0 a 5 FAIL sul gate
di default** e l'estensione lenta da 18 a 23. I due pavimenti non sono la stessa
manopola: questo arma una correzione *rapida*, e una soglia più bassa la arma su
evidenza che non si è ancora assestata. Lasciarlo stare.

- [ ] **Resta `confirm=-1.000` a 52 BPM**: il percorso di recupero rapido non si
  conferma mai lì, ed è il motivo per cui i 18 casi restano FAIL nonostante
  `stable` sia passato da mai a 3.4 s. È quel `persistentFloor` da 46 ms, e la
  prova qui sopra dice che non si tocca da solo.
- [ ] **3.4 s è ancora tanto** contro una battuta di 4.6 s a 52 BPM. Meglio di
  «mai», non ancora «subito».
- [ ] Da riascoltare sul materiale dell'utente: la misura dice che il residuo
  permanente non c'è più, non dice che l'orecchio sia contento.

---

### 33. Il batterista suona solo charleston in quarti 🟢 (2026-09-10, misurato: il tempo è perfetto, il problema è l'uno)

Segnalazione: *«a volte il batterista suona solo il charleston in quarti. L'app
ha difficoltà a capire e seguire il tempo.»*

**Misurato, e il tempo non è il problema.** Charleston chiuso in quarti, niente
cassa, niente rullante, tre secondi di silenzio prima come in un brano vero:

| BPM | primo aggancio | dentro il ±2% | ottava |
|---|---|---|---|
| 60 | 6.23 s | **100.0%** | giusta |
| 76 | 6.23 s | **99.1%** | giusta |
| 96 | 2.56 s | **99.3%** | giusta |
| 132 | 4.77 s | **99.7%** | giusta |

Confidenza 1.00, pettine esatto a 96.00, residuo 0.002, copertura 1.00. Su
questo materiale il tempo è tenuto meglio che su quasi tutto il resto del banco.

**Il problema è la battuta.** Colonna `1?` nuova nella traccia di `VPTrack`
(`barTrusted`): **`no` per tutta la durata**, e resta `no` anche con un accento
sull'uno. È il comportamento *giusto* — un charleston piatto non contiene
nessuna informazione sul downbeat, e la rete lo sa — ma le conseguenze si
sentono:

- il **clap** correttamente non entra (è cancellato da `barTrusted`);
- la figura delle **congas** deve comunque scegliere un uno, e su un charleston
  piatto è testa o croce.

Il risultato all'orecchio è: la pulsazione è giusta, gli accenti sono nel posto
sbagliato. Per un musicista quello suona come «non ha capito il pezzo», anche
se il BPM è esatto al centesimo. **È quasi certamente questo che l'utente
sente**, non un errore di tempo.

La risposta è la stessa dell'ottava: quell'informazione non è nell'audio, e
l'app ha già il comando per riceverla dall'esterno — **TAP / SPOSTA L'1**. Su
materiale che non porta il downbeat non è un ripiego, è l'unica sorgente
possibile.

- [ ] **Da verificare all'orecchio**: quando succede, la pulsazione è giusta e
  l'accento è spostato? Se sì è questo item e la cura è il tap. Se il polso
  stesso vacilla, è un altro problema e serve una registrazione.
- [ ] **Buco stretto introdotto dall'item 29**: `rhythmSeen` si aggancia sulla
  quota di banda bassa, e un charleston ne ha **zero** (`lowS = 0.000` misurato).
  Un brano che parte dal silenzio entra comunque dal suo epoch — verificato, la
  parte suona — ma se si preme START **durante** un passaggio di solo charleston,
  senza nessun momento di quiete prima, `alreadyPlaying` non si apre e la parte
  non entra finché non arriva qualcosa con del basso. Il TAP la libera.
  La riparazione onesta sarebbe un secondo modo di agganciare `rhythmSeen` — per
  esempio una griglia pulita e stabile su cui pettine e rete concordano, che
  l'intro di BLUE SKY non produce mai — ma va misurata contro `probe_room` e
  `VPTests --makeup` prima, perché la stanza vuota raggiunge FOLLOWING a 0.91 di
  confidenza e non deve poter aprire questo cancello.

**Nota sulla priorità del modello.** Questo caso è materiale *senza melodia e
senza basso*, ed è proprio quello per cui si sarebbe detto «serve un modello
diverso». Non serve: la rete lo tiene al centesimo. Vale come dato quando si
rivaluta quanto in alto sta davvero il modello nella lista.

---

### 34. Prima misura su una band vera: `Flamingo Marco 09.07.26` 🔴 (2026-09-10, misurato, non corretto)

Registrazione di una serata intera, **97 minuti, mandata del banco** — cioè lo
stesso tipo di segnale che l'app riceverà sul palco. È il primo materiale vero
che questo progetto abbia mai misurato. Segmentazione dai livelli e dal tempo:

| # | dal | al | tempo |
|---|---|---|---|
| 1 | 0:10 | 4:40 | ~82 (l'utente ci mette lo SWING) |
| 2 | 5:10 | 9:30 | ~72 / 107 |
| 3 | 10:10 | 14:50 | ~104 — *Sally*, segnalato come il peggiore |
| 4 | 15:10 | 19:40 | ~86 |
| 5 | 20:20 | 24:20 | ~89 |
| 6 | 25:30 | 27:50 | ~68 / 103 |

#### Brano 3 (Sally): l'app oscilla il 40% più del batterista

Stima indipendente del tempo vero con un tempogramma a finestra di 12 s
(autocorrelazione del flusso spettrale, solo i punti con nitidezza > 0.15).
**Non è verità di fase** — è un secondo parere — ma basta per il tempo:

| | |
|---|---|
| il batterista | 101.4-107.2 BPM, dev.std **1.6%** |
| l'app | 95.4-109.0 BPM, dev.std **2.2%** |
| errore medio dell'app | **1.34%**, peggiore **7.78%** |
| errore medio del **pettine** | **0.96%** |
| uscite oltre il 4% | 2, di cui una da **10 secondi** |

Il batterista oscilla davvero — è esattamente «un batterista non professionista
senza clic» — ma l'app amplifica. E **il pettine è più vicino al vero del numero
che l'app pubblica**: una delle sue sorgenti batte il suo risultato.

L'uscita da 10 secondi, per battito:

```
 18.0  pubbl 103.43   rete 103.43   pettine 102.92   cop 1.00
 20.0  pubbl  95.64   rete  95.64   pettine 103.27   cop 1.00   <- la rete crolla
 22.0  pubbl  95.57   rete  95.50   pettine 102.92   cop 0.80
 26.0  pubbl  95.37   rete  95.63   pettine 102.21   cop 0.62
 28.0  pubbl  95.59   rete  95.60   pettine 102.56   cop 0.00
 30.0  pubbl 101.38   rete 101.47   pettine 104.35   cop 0.00
 34.0  pubbl 102.47   rete 102.45   pettine 103.63   cop 1.00
```

La rete scende a 95.6 e ci resta dieci secondi; **il pettine dice 102-104 per
tutti e dieci**, e la copertura della griglia crolla a zero — la griglia a 95.6
sta rifiutando i battiti veri. Il disaccordo è `log2(103.27/95.64) = 0.111`,
cioè l'**8.0%**: sopra `kCombPullThreshold` (3%) e **sotto**
`kStaleGridThreshold` (8.7%). Nessuno dei due meccanismi decisivi lo possiede;
agisce solo la trazione lenta verso il pettine, 35% di autorità × 22% per
battito = **7.7% effettivo, tredici battiti, tre battute**. Che sono i dieci
secondi.

#### Due tentativi, entrambi misurati e scartati

**1. Dare più autorità al pettine (`kCombPull`).** Non è monotono:

| `kCombPull` | uscite | durate | totale fuori | errore medio |
|---|---|---|---|---|
| **0.35** (base) | 2 | 10 / 2 s | 12 s | 1.34% |
| 0.45 | 4 | 22 / 4 / 6 / 2 s | **34 s** | 1.64% |
| 0.55 | 3 | 6 / 6 / 2 s | 14 s | 1.30% |
| 0.65 | 3 | 10 / 12 / 2 s | 24 s | 1.31% |

È lo stesso fenomeno a bacini già documentato nell'item 29 per il guadagno
d'analisi. **Su un brano solo una costante non si tara: si fitta il rumore.**
Ripristinato a 0.35.

**2. Abbassare `kStaleGridThreshold`** da 0.120 a 0.070, per coprire la banda
5-8.7% in cui questo caso cade. `probe_matrix` **identico** (98 uscite, 5.25 s,
8.98%) — su quel banco la banda non scatta mai — ma anche la traccia di Sally è
**identica byte per byte**: il watchdog non arriva comunque a votare, e il
motivo non è la soglia. Ripristinato a 0.120.

#### Brano 1 (swing): ottimo dentro, difficile all'inizio

Da 30 s a 270 s tiene **82 BPM** con confidenza fino a 1.00, copertura 1.00 e
`barTrusted` = SI. Il problema sono i primi venti secondi:

```
   2.0  gAnalisi 23.99   picco 0.001            <- guadagno al tetto, ingresso muto
   6.0  pubbl 105.27     picco 0.000            <- pubblica 105 sul nulla
  16.0  pubbl 116.01     picco 0.050  suona SI  <- entra la band, e la parte entra su 116
  16.7  epoch
  20.0  pubbl  81.53                            <- giusto
```

Sedici secondi di ingresso praticamente muto (`picco` 0.000-0.001), il make-up
al tetto di 24×, e la rete che risponde al rumore amplificato con 105 BPM a
confidenza 0.62. Poi **quattro secondi di parte suonata su 116 BPM** prima che
l'epoch la corregga.

- [x] **Difetto del cancello dell'item 29, misurato qui — CORRETTO (2026-09-14).**
  Con il guadagno a 24× su un ingresso muto, `lowS` legge **0.34-0.46** — cioè la
  quota di banda bassa del *rumore amplificato*, che è alta perché il rumore è
  tutto in basso. Quindi `rhythmSeen` si agganciava sul silenzio. Qui non era
  stato lui a far entrare la parte (l'ha tenuta fuori il test di livello,
  `picco` 0.001 < 0.040), ma era un aggancio falso.
  **Fix:** `updateRhythmShare(numSamples, sourceAudible)` non vota sotto il
  pavimento udibile — lo stesso `sourcePeak > (speaker ? 0.004 : 0.040)` che usa
  `setSourceAudible`, calcolato una volta in `process` e passato a entrambi. I
  filtri continuano a girare (il plateau resta caldo), si ritira solo il voto.
  `rhythmSeen` è ora anche nello snapshot (`EngineSnapshot::rhythmSeen`, mirror
  atomico). Regressione `VPTests --rhythm` (3/3): tono a 100 Hz sotto il
  pavimento → `lowShare 0.650` ma `seen 0`; stesso tono a livello band
  (`lowShare 0.639`) → `seen 1`. Controprova senza il cancello: `quiet seen 1`,
  1 FAIL. Gate verdi: `--bar` 10/0, `--new-input` 3/0, `--swing` 3/0,
  `--evidence` 2/0, `--tempo-slow` 10/0, `--state-timing` 3/3, `--makeup c` 6/0,
  `e` 4/0, `f` 7/0 (`d` resta 10/2, gli stessi due fail presenti su HEAD).
- [ ] **I quattro secondi su 116 BPM** non li avrebbe evitati: dopo l'epoch il
  cancello è aperto comunque da `sawInputStart`, e il decoder ha bisogno di quei
  secondi per riacquisire. È un problema diverso.

#### Cosa serve adesso, ed è la conclusione operativa

L'utente chiede: *«se succedono live troppe uscite crea difficoltà; dovrebbe
almeno essere velocissimo a riprendersi»*. La velocità di rientro **è** quella
trazione da 7.7% per battito, e la misura qui sopra dice che non si può tarare
su un brano.

- [ ] **Costruire un banco dai sei brani di questa registrazione**, con la stima
  indipendente del tempo per ciascuno, e tarare contro quello. È la prima volta
  che c'è materiale per farlo. `probe_matrix` resta il banco sintetico; questo
  diventa il banco reale, e i due vanno guardati insieme.
- [ ] Il tempogramma indipendente (`scratch/tempocurve.py`) va portato in
  `scripts/` e reso ripetibile, altrimenti il banco non esiste.

---

### 35. I salti di fase mentre la parte suona, e come diventano «il clap in battere» 🔴 (2026-09-10, misurato su band vera — causa trovata, non corretta)

Segnalazione, sul live `Flamingo Marco 09.07.26`: *«se il batterista rallenta o
velocizza tipo di 2 bpm, l'app non riaggancia immediatamente, nonostante si
senta che la percussione è fuori da cassa o rullo. Poi a un certo punto
addirittura l'app accelera talmente tanto che sposta l'uno al quarto successivo,
quindi il clap suona addirittura in battere.»* E sul brano 1: *«esce
costantemente e resta spesso fuori, in certi punti aumenta senza senso
esageratamente.»*

**Il BPM pubblicato non spiega niente di tutto questo**, ed è per questo che
non si era mai visto: sul brano 1 sta fra 80.5 e 84 per tutto il corpo del pezzo,
con confidenza fino a 1.00 e copertura 1.00. La colonna che l'utente sente è
un'altra.

#### Lo strumento che mancava

`VPTrack --pulses file` scrive, blocco per blocco, la **fase dell'orologio**
(battito e battuta) accanto al tempo pubblicato. Da lì si ricavano due cose che
prima non si potevano vedere:

- la **velocità istantanea della griglia**, cioè quella su cui i colpi sono
  davvero programmati, come derivata della fase;
- i **salti**, cioè i blocchi in cui la fase si sposta molto più di quanto il
  tempo preveda.

E `s.clockBpm` è stato aggiunto allo snapshot. Nota: **è uguale a `s.bpm`** —
`tr.clock.tempoBpm` è il bersaglio, non la velocità istantanea — quindi la
deriva va ricavata dalla fase, non da lì. Costa un tentativo saperlo.

#### Cosa dicono i numeri

Velocità istantanea della griglia contro il tempo dichiarato:

| | dichiarato | griglia istantanea | strappi oltre il 4% |
|---|---|---|---|
| brano 1 (swing) | 82.3 | **65.7 - 92.2** | 6 volte, 7 s su 245 |
| brano 3 (Sally) | 104.4 | **23.4 - 123.8** | **32 volte, 27 s su 269** |

E i salti veri, letti sulla fase grezza — ognuno in **un solo blocco da 2.6 ms**,
con la parte che suona:

| a | salto fase battito | in ms | salto fase battuta |
|---|---|---|---|
| 27.42 s | −0.438 battiti | **−275 ms** | −0.109 |
| 49.08 s | −0.302 battiti | **−169 ms** | −0.074 |
| 216.54 s | −0.214 battiti | −124 ms | −0.052 |
| 264.60 s | +0.373 battiti | +205 ms | +0.094 |

Il brano 1 ne ha **zero**: il suo problema è la deriva continua, non i salti.

#### Come si diventa «il clap in battere»

La fase della battuta si sposta ogni volta di **esattamente un quarto** del
salto del battito (−0.438/4 = −0.109, −0.302/4 = −0.076, +0.373/4 = +0.093):
il conteggio viene trascinato con la griglia, com'è giusto per un singolo
salto — ognuno è sotto il mezzo battito, quindi preso da solo è una correzione
di fase legittima.

**Ma si sommano.** I tre salti all'indietro fanno **0.954 battiti**, meno 0.373
in avanti: **0.58 battiti netti** di spostamento del conteggio rispetto alla
musica, in un brano solo. Ripetuto, l'uno arriva sul quarto successivo. Nessun
salto singolo è illegittimo; la somma sì, e niente la sorveglia.

#### Dove sta nel codice

Due punti chiamano `snapPhase` con la parte in corso:

- `BeatTracker.cpp:1421` — dopo una ricostruzione della griglia. Ben educato:
  mette `sounding = false` e `waitForQuantize`, cioè **ferma la parte e la fa
  rientrare quantizzata**.
- `BeatTracker.cpp:1561` — sulla transizione in FOLLOWING:
  `follower.snapPhase (songPhase, ! sounding)`, con l'unica condizione
  `|errore| > 0.12` battiti. **Nessun limite superiore, e la parte non viene
  fermata.** È da qui che passano i 275 ms.

Il commento accanto dice: *«Sounding, a snap is a stroke... Above the size the
steering loop would take seconds over, it is worth the stroke»*. Il ragionamento
regge per 0.15 battiti. Per 0.44 non regge: quello non è un flam, è la parte che
si sposta di un quarto di battito sotto le mani di chi suona.

#### CORRETTO: la risposta al drop di griglia è graduata (10/09/2026)

**Prima due correzioni a quello che c'è scritto sopra**, entrambe trovate
strumentando invece di ragionare:

1. **Non è l'accumulo.** Avevo scritto che i salti nella stessa direzione si
   sommano fino a spostare l'uno. Falso: al sito che li produce
   `keepBarInStep` è già `true`, quindi **il conteggio segue la griglia**. Il
   rilevatore di «battuta ruotata» che avevo scritto scattava proprio quando il
   conteggio seguiva *correttamente*. Era sbagliato lo strumento.
2. **Non è il sito 1561.** Ci ho messo il tetto per primo e i salti sono rimasti
   **identici**: `VP_SNAP_LOG` su ogni chiamata dice che **tutti e sette** i
   salti grossi vengono dal sito **1421**, il percorso di ricostruzione della
   griglia.

**La catena vera.** `BeatDecoder::checkGridPhase` fa scorrere l'ancora della
griglia sulla fase del fold — è una correzione di *fase*, non un tempo nuovo —
e incrementa `gridSerial`. Il tracker legge `gridSerial` come «pulsazione
nuova» e reagisce nel modo previsto per quel caso: **ferma la parte, mette la
griglia esattamente sul battito accettato, rientra quantizzato**. E
`checkGridPhase` piega il suo scarto sulla griglia più vicina, quindi può
legittimamente valere **fino a mezzo battito**.

Strumentato su Sally, tutte e sette le uscite vengono da lì:

```
GRID DROP [reset] t= 27.4  bpm= 95.5  comb=102.2
GRID DROP [reset] t= 49.0  bpm=109.3  comb=104.3
GRID DROP [reset] t=216.5  bpm=102.7  comb=101.5
GRID DROP [reset] t=264.5  bpm=107.8  comb=103.8   ... e altre tre
```

**La correzione: la risposta è graduata come quella di un musicista.** Sopra un
terzo di battito la griglia è davvero altrove e fermarsi per rientrare è
giusto. Fra un ottavo e un terzo è una piegata: si sposta subito una parte, con
un tetto di 0.20 battiti, e il resto lo chiude l'anello di fase — **senza
fermare la parte**, perché un percussionista che è un quinto di battito fuori
non si ferma, si appoggia.

| Sally | prima | dopo |
|---|---|---|
| salti di fase | **7** | **3** |
| la parte si ferma e rientra | **7 volte** | **1 volta** |
| correzioni di mezza taglia | 0.31-0.39 battiti | **tagliate a 0.195** (116 ms) |

Il salto da 0.438 resta e prende la strada del fermarsi-e-rientrare, che per
quella taglia è la cosa giusta.

Regressioni: `VPAlign` **identico**, `--bar` 10/0, `--tempo-slow` 10/0,
`--level` 15/1, `probe_bar` con **un battito su 2831** che cambia casella
(0:1793→1792, 1:399→400) e tutto il resto invariato, ingresso sull'uno e
rotazioni comprese.

Il tetto messo anche al sito 1561 è rimasto: non è lui a produrre questi salti,
ma passava `! sounding` a `keepBarInStep`, e `snapPhase` documenta da sé che
così «the count is silently rotated by a quarter». Ora passa `true` in entrambi
i casi.

- [ ] **Il brano 1 non ha salti** e sta comunque fuori: 6 strappi oltre il 4%
  con la griglia che scende a 66 BPM contro 82 dichiarati, e nessun drop di
  griglia nel corpo del pezzo. Quello è il percorso di *steering*, ed è un
  secondo difetto, indipendente da questo.
- [ ] **Perché `checkGridPhase` scatta sette volte** in un brano resta da
  capire: è il fold che continua a dire che la griglia è sfasata. Se il fold ha
  ragione, il vero difetto è a monte; se ha torto, va guardata la sua soglia.
- [ ] **`gridSerial` fa due lavori.** Lo incrementano sia un cambio di
  pulsazione vero (snap d'ottava, watchdog) sia una correzione di fase. Il
  consumatore non può distinguerli e la risposta giusta è diversa. Separarli
  sarebbe più pulito di graduare a valle sulla taglia dello scarto.
- [ ] **Il brano 1 non ha salti** e sta comunque fuori: 6 strappi oltre il 4%
  con la griglia che scende a 66 BPM contro 82 dichiarati. Quello è il percorso
  di steering, non lo snap, ed è un secondo difetto da misurare a parte.

#### Il banco nuovo

`scratch/hist.py` piega l'energia degli attacchi sulla fase dell'orologio e
misura quanto la griglia sta sulla musica (struttura = picco/media
dell'istogramma a 24 bin) e dove sta il picco. È robusto alle terzine, che il
primo tentativo — la risultante della prima armonica — non era: su materiale
swingato l'energia cade a 0, 1/3 e 2/3 e la risultante si annulla **anche con
l'aggancio perfetto**. Il primo tentativo dava «FUORI» su tutto il brano 1 ed
era un artefatto della metrica. Va portato in `scripts/`.

---

### 36. `03 FEEL`: l'ottava doppia su un brano intero, e il bilancio dei tre brani veri 🔴 (2026-09-10)

Terzo file reale, 4:19. **Il tempo vero è ~104 BPM e l'app suona a 205-213 per
tutto il brano**: conta gli ottavi come battiti. Autocorrelazione della banda
alta (charleston e piatti) sul tratto 30-150 s:

| periodo | 52 | 86 | **104** | 138 | **208** |
|---|---|---|---|---|---|
| autocorrelazione | 0.238 | −0.004 | **0.371** | 0.005 | 0.289 |

104 vince su 208 con margine. La banda bassa è debole ovunque (0.005-0.036): la
cassa è sparsa o coperta, quindi il livello metrico deve venire dalla banda
alta, che è piena di ottavi — è **esattamente** il caso che la skill descrive
come «a dense grid is not a right grid»: il charleston riempie gli ottavi,
quindi la griglia doppia ha copertura 1.00, residuo 0.03 e sembra sana a ogni
test che il veto possiede.

Peggio: `barTrusted` è **SI** da 40 s. L'app piazza con fiducia una battuta su
una griglia doppia, quindi l'uno cade su quello che la band sente come «e».

- [ ] Verificare se `recentStrengthAlternation()` scatta qui. Esiste apposta per
  questo caso e la misura dice che non ha corretto. Se non scatta, capire
  perché; se scatta e viene vetata, capire da cosa.
- [ ] `probe_matrix` ha una riga `rock 8vi` che è lo stesso difetto sintetico
  (rapporto 1.44 a 81 BPM). Questo file è la sua versione reale e va aggiunto
  al banco.

#### Il bilancio dei tre brani veri

| | brano 1 (swing, 82) | Sally (104) | FEEL (104) |
|---|---|---|---|
| ottava | giusta | giusta | **doppia** |
| salti di fase | 0 | 7 → **1** | 3, tutti sotto i 15 s |
| la parte si ferma | 0 | 7 → **1** | 1, in acquisizione |
| strappi griglia > 4% | 6 | 32 | 4, tutti sotto i 27 s |
| deriva di fase | ±60-170 ms | ±70-170 ms | ±12-60 ms |

**Tre difetti diversi, non uno.** La correzione dell'item 35 ha chiuso quello di
Sally e non tocca gli altri due. Chi riprende non deve cercare una causa sola.

#### Cosa costa davvero «rientrare subito», in quattro pezzi indipendenti

Misurato attraverso tutti e tre i brani, il rientro ha quattro costi in serie, e
ognuno ha un padrone diverso:

1. **Accorgersene.** Il fit lungo è 8-24 battiti, e a tempo lento la sua
   finestra è nove secondi; il fold è l'altra via. Sul brano 1 è questo il costo
   vincolante (item 31: quattro secondi solo per uscire da FISSO).
2. **Decidere.** L'arbitrato rete/pettine: 35% di autorità × 22% per battito =
   7.7% effettivo, **tredici battiti**. Misurato non tarabile su un brano solo
   (item 34), serve il banco dei sei.
3. **Muoversi.** L'orologio: o un salto (istantaneo ma è uno strappo sotto le
   mani) o lo steering, limitato al 20%. **Graduato nell'item 35.**
4. **Il pavimento.** Quanto vicino può arrivare: era 13.8 ms a 52 BPM, ora 6 ms
   a ogni tempo (item 32).

Corretti oggi il 3 e il 4. Aperti l'1 e il 2.

**E il pavimento fisico va detto:** l'orologio piega la velocità invece di
saltare, apposta, perché nessun colpo venga mai suonato due volte o saltato. Al
rail del 20%, chiudere un quarto di battito costa **1.25 battiti** — circa un
secondo a 104 BPM. Quello è il minimo per una correzione che non si senta come
uno strappo. Più veloce di così è un salto, ed è esattamente quello che l'item
35 ha appena messo sotto tetto. **«Immediatamente» ha come pavimento un
battito**, e per avvicinarcisi si lavora sul punto 1, non sul punto 3.

---

### 37. La deriva leggera durante il brano: il default di `FollowStrength` era il peggiore dei tre 🟢 (2026-09-10, misurato su band vera e corretto)

Richiesta: *«risolvere i problemi di drift leggeri delle percussioni durante il
brano, rendere tutto il più preciso possibile»*.

**Il conto di partenza.** Un errore di tempo dell'1% è una deriva di fase di
10 ms al secondo. A 82 BPM, l'app che gira a 82.8 invece di 82.0 guadagna 170 ms
in diciassette secondi — che è l'ordine di grandezza misurato sui brani veri
(±60-170 ms). La deriva di fase *è* in gran parte il tempo leggermente sbagliato,
più l'anello che la insegue.

**Quanto stringe l'anello lo decide `FollowStrength`**, e la tabella in
`TempoFollower` è (tau, steerLim, steerCeil, dGain):

| | tau | steerLim | steerCeil | dGain |
|---|---|---|---|---|
| low | 1.60 | 0.018 | 0.10 | 0.3 |
| medium | 0.90 | 0.035 | 0.18 | 0.8 |
| high | 0.70 | 0.050 | 0.25 | 1.2 |

`docs/STATUS.md` e la skill registravano già che **HIGH fa escursioni due o tre
volte più grandi di LOW senza guadagno in rms** su `probe_steer` (6.7% contro
2.6% di peggiore a 144 BPM), e che il default era stato lasciato su HIGH perché
*«quel banco non ha dentro nessun cambio di tempo vero, che è l'unica cosa per
cui HIGH esiste»*.

**Adesso c'è materiale con cambi di tempo veri** — una band dal vivo che
respira — e **LOW vince lo stesso.** `VPTrack --follow` (nuovo) sui due brani:

| brano 1 (82 BPM) | struttura | spost. mediano | peggiore | strattoni dev.std |
|---|---|---|---|---|
| **low** | **3.88** | **30.3 ms** | **182 ms** | **1.77%** |
| medium | 3.77 | 60.7 ms | 152 ms | 1.81% |
| high *(default)* | 3.61 | 30.3 ms | **364 ms** | 2.11% |

| Sally (104 BPM) | | | | |
|---|---|---|---|---|
| **low** | **3.29** | **47.9 ms** | **240 ms** | **2.86%** |
| medium | 3.14 | 95.7 ms | 263 ms | 3.00% |
| high *(default)* | 3.04 | 71.7 ms | 263 ms | **3.57%** |

LOW vince su struttura (entrambi), spostamento mediano (entrambi) e dev.std
degli strattoni (entrambi).

**E `--level` lo conferma sul sintetico**, sulla colonna della fase a 91 BPM:

| livello | HIGH | LOW |
|---|---|---|
| 0 dB | 59.0 ms | **33.1 ms** |
| −6 dB | 25.6 ms | **14.4 ms** |
| −12 dB | 32.3 ms | **14.4 ms** |
| −18 dB | 33.2 ms | 42.8 ms (peggio) |

A 168 BPM è mezzo millisecondo peggio (6.6 → 7.1), su numeri già dentro il
bersaglio degli 8 ms.

**Corretto: il default passa a LOW.** Era `high` in due punti, entrambi cablati
— `Types.h:368` e `MainComponent.cpp:929` — e **non esiste nessun controllo
nell'interfaccia**: non era una manopola, era una scelta fissa.

Regressioni: `--level` **15/1** (stesso unico FAIL), `--bar` 10/0,
`--tempo-slow` 10/0, `VPAlign` **identico riga per riga** (imposta le tarature
esplicitamente per riga, quindi il default non lo tocca).

#### Il costo di LOW sui gradini: misurato, ed è zero

Il dubbio lasciato aperto qui sopra è chiuso. Due fixture nuove attraverso il
motore completo — kick/rullante/charleston, 100 BPM per 63 s poi il gradino,
`VPTrack --follow` — e si misurano due cose separate: quando il **tempo
dichiarato** entra nel ±2% e ci resta due secondi, e quando ci entra la
**velocità istantanea della griglia**, che è quella su cui i colpi cadono.

| gradino | taratura | tempo dichiarato | griglia vera | sovraelongazione |
|---|---|---|---|---|
| 100 → 124 (+24%) | high | 16.7 s | 19.1 s | +0.3% |
| 100 → 124 (+24%) | **low** | **16.7 s** | **19.1 s** | +0.2% |
| 100 → 107 (+7%) | high | 1.2 s | 2.2 s | +7.1% |
| 100 → 107 (+7%) | **low** | **1.2 s** | **2.2 s** | +7.7% |

**Identici al decimo di secondo.** LOW non costa niente sui gradini, e il motivo
è strutturale: `FollowStrength` governa la *fase*, mentre il costo di un gradino
sta nell'*accorgersene*, che è il decoder e non cambia. Il compromesso che si
temeva non esiste; LOW è un guadagno netto.

- [x] ~~Il costo di LOW su un gradino di tempo brusco non è misurato~~ — misurato,
  è zero.

#### Ma le due righe dicono un'altra cosa, e va perseguita

**Una spinta realistica del 7% — un batterista che accelera — costa 1.2 s per il
tempo e 2.2 s per la griglia, con una sovraelongazione del 7%.** La griglia va a
~114 BPM quando la band è andata a 107, poi rientra. Segnalato dall'utente in
due modi diversi: *«quando deve saltare tanto di bpm lo fa accelerando o
decelerando invece di passarci direttamente»* e *«su Baila e su tanti ci sono
punti in cui il batterista rallenta o accelera e l'app arranca un po' prima di
riallinearsi»*.

La sovraelongazione **non è un difetto dell'anello**: è la fase accumulata che
viene ripagata. Il tempo ci mette 1.2 s a essere notato, in quel tempo la fase
accumula, e la griglia deve correre più veloce del bersaglio per recuperarla.
La sua taglia è limitata da `steerCeil` (0.10 a LOW, 0.25 a HIGH).

Quindi la scala è: **meno ritardo nell'accorgersi = meno fase accumulata = meno
sovraelongazione.** Non si riduce stringendo l'anello — si riduce accorgendosene
prima, che è il punto 1 del bilancio dell'item 36. È la stessa conclusione da
tre direzioni diverse.

- [ ] Il gradino da +24% a **16.7 s** è un numero pessimo, ma un salto del 24%
  è un altro brano, non un batterista. Non confondere i due casi: la riga da
  perseguire è quella del 7%.
- [ ] **Non c'è un controllo per questo.** Tre tarature nel codice, nessun modo
  di sceglierle. Se il palco vuole HIGH per un pezzo e LOW per un altro, oggi
  non si può.
- [ ] **La deriva residua resta il punto 1** del bilancio dell'item 36:
  accorgersi prima. LOW stringe l'anello, non accelera l'accorgersi.

---

### 38. Punto 1, «accorgersene prima»: il banco reale, il numero, e perché non si tara 🔴 (2026-09-10)

Richiesta: *«lavora sul punto 1: accorgersene prima»*. Prima di toccare
qualunque cosa serviva **misurare il ritardo**, che fino a oggi era solo dedotto.

#### Il banco: `scripts/analysis/bench_live.py`

Cinque brani dal centro del live `Flamingo Marco 09.07.26`, 2:20 ciascuno, presi
dal **centro** del pezzo per evitare intro e code, che sono un altro problema.
Per ognuno una stima indipendente del tempo vero (`tempocurve.py`, tempogramma a
finestra di 12 s). Il batterista deriva del **2.6-4.2% dentro ogni brano**:
80.7-84.1, 105.0-108.3, 101.6-105.7, 84.4-86.8, 87.6-89.9 BPM.

Riporta tre difetti separati:

- **ritardo** — di quanti secondi l'app è indietro, dal massimo della
  correlazione incrociata. È il punto 1.
- **errore** — scarto medio del passo, **ripiegato sull'ottava**, perché
  altrimenti un errore del 100% seppellirebbe tutto il resto.
- **strattoni** — deviazione standard della velocità *istantanea* della griglia.

Due cose imparate costruendolo, entrambe costate una corsa:

1. **Serve un lead-in di silenzio.** Tagliando a metà brano non c'è nessun
   momento di quiete, quindi `alreadyPlaying` non si apre e **la parte non entra
   mai** — è il buco stretto dell'item 33, che ha morso il mio stesso banco.
   Tre secondi di silenzio davanti a ogni taglio, e le curve di verità spostate
   di altrettanto.
2. **Il ritardo non è misurabile quando la correlazione è bassa.** Con r=0.17 un
   brano ha riportato **−4.00 s**, cioè l'app *in anticipo* sul batterista, e
   trascinava la media. Sotto r=0.50 il ritardo si stampa ma non entra
   nell'aggregato.

#### La base, confermata due volte

| brano | ritardo | r | errore | strattoni | ottava |
|---|---|---|---|---|---|
| 1 | +7.75 s | 0.85 | 1.09% | 2.28% | — |
| 2 | +5.25 s | 0.85 | 0.72% | 1.74% | — |
| 3 | +3.50 s | 0.49 | 1.12% | 2.25% | — |
| 4 | +7.50 s | 0.81 | 0.78% | 1.17% | **100%** |
| 5 | +5.00 s | 0.31 | 0.99% | 1.11% | **100%** |
| **MEDIA** | **+6.83 s** | 0.66 | **0.94%** | **1.71%** | **40%** |

**Il numero del punto 1 è ~6.8 secondi.** L'app è quasi sette secondi indietro
rispetto alla deriva del batterista. Il *passo* invece è preciso: 0.94% una
volta tolta l'ottava.

**E due brani su cinque sono suonati all'ottava sbagliata.** Con *Baila*, *FEEL*
e questi, fanno **quattro su otto** dei brani veri misurati finora. L'item 36
non è un caso isolato: è il difetto più frequente sul materiale reale.

#### Il banco è ripetibile, e verificarlo era necessario

Tre corse dello stesso binario danno serie di BPM **byte-identiche**, e la corsa
completa ripetuta dà la stessa media al centesimo. `VPTrack` aspetta già
`analysisCompletedSamples()` a ogni hop.

**Ma una base precedente, 5.30 s, non è riproducibile** e non so da quale
binario venisse: stesso codice, stessi file di verità, banco deterministico, e
oggi lo stesso comando dà 5.80 (poi 6.83 con il filtro su r). **Quella base è
scartata e con essa lo sweep che ci era stato misurato contro.** Chi riprende:
il baseline si rimisura, non si eredita.

#### `kRateLive`: il compromesso è reale e non è un guadagno

L'aritmetica dice che una piegatura del primo ordine a α per battito lascia
(1−α)/α battiti di ritardo: a 0.22 sono **3.5 battiti**, che spiegano metà del
ritardo misurato. Alzarla:

| `kRateLive` | ritardo (r≥0.5) | strattoni |
|---|---|---|
| **0.22** (base) | **6.83 s** | **1.71%** |
| 0.32 | 5.67 s | 2.81% |

Guadagna 1.2 s di ritardo e **triplica gli strattoni sul brano 2** (1.74% →
5.38%). Non è un guadagno pulito, ed è la seconda costante di seguito che si
comporta così. **Non spedita.**

- [ ] **La strada non è tarare un guadagno.** Due sweep di fila
  (`kCombPull` item 34, `kRateLive` qui) sono risultati non monotoni o con un
  costo pari al guadagno. Il ritardo è strutturale: fit centrato + piegatura del
  primo ordine. Si toglie cambiando la *forma* della stima, non il suo guadagno
  — per esempio una stima non centrata (fit pesato verso i battiti recenti) o un
  predittore esplicito della deriva invece di un filtro che la insegue.
#### `kLiveLead` serve, e il difetto è che esiste in un ramo solo

Strumentato (`VP_LEAD_LOG`, poi rimosso), sul brano 1 che sta in `live` per il
72% del tempo: `haveLong` è vero al **99%**, l'anticipo applicato vale **0.369
BPM in media, lo 0.447% del tempo**, e **non viene mai clampato**. Funziona.

Sui brani 2 e 4 il log non stampa nulla: quel ramo non viene quasi mai
raggiunto. La distribuzione dei regimi sul banco lo spiega:

| brano | CERCO | FISSO | VIVO | ritardo |
|---|---|---|---|---|
| 1 | 4% | 23% | **72%** | +7.75 s |
| 2 | 0% | **63%** | 35% | +5.25 s |
| 3 | 3% | 27% | 69% | +3.50 s |
| 4 | **69%** | 0% | 30% | +7.50 s |
| 5 | 25% | 0% | 73% | +5.00 s |

**L'estrapolazione in avanti esiste solo nel ramo `live`.** Gli altri due usano
stime senza nessun anticipo, e sono le più in ritardo che ci siano:

- `unknown` prende il **fit lungo grezzo** — 24 battiti, centrato **12 battiti
  indietro**, che a 86 BPM sono **8.4 s** di puro ritardo di centratura. Il
  brano 4 ci passa il 69% del tempo e misura +7.50 s. I due numeri sono lo
  stesso numero.
- `fixed` prende `fixedAnchorBpm`, una media corrente del fit lungo: ritardo
  doppio.

#### Provato: dare l'anticipo anche a `unknown`. Misurato bene e misurato male.

| | base | con l'anticipo in `unknown` |
|---|---|---|
| **brano 4** | +7.50 s | **+3.00 s** |
| banco reale, media | 6.83 s | **5.50 s** |
| correlazione media | 0.66 | **0.70** |
| errore / strattoni | 0.94% / 1.71% | 0.97% / 1.77% |
| `probe_matrix` aggancio medio | 5.25 s | **4.88 s** |
| `probe_matrix` uscite | 98 | **97** |
| `probe_tempo_step` | — | **identico** |
| **`VPAlign` a 132 BPM, fase rms** | **6.1 ms** | **118.9 ms** |
| `VPAlign` scatti a 100 e 132 BPM | 0 e 0 | **27 e 33** |

Il fit lungo è il **preciso**; il corto porta con sé il proprio rumore
(0.5-1.15 BPM battito-per-battito a tempo fermo). Su materiale pulito quel
rumore diventa fase, e VPAlign lo dice in modo brutale.

**Provato: gattare l'anticipo su «il tempo si sta muovendo»** (`haveWindow &&
|trend| > kLiveTrend`, lo stesso test della macchina dei regimi). `VPAlign`
torna **identico alla base** — e il banco reale torna **identico alla base
anche lui**: l'anticipo non scatta mai. Ed è circolare per costruzione: quei
brani stanno in `unknown` **proprio perché** quel test è falso. Ripristinato.

È lo stesso muro documentato nel ramo `live`, raggiunto dall'altra parte: *«il
movimento del fit corto a tempo fermo è 0.5-1.15 BPM, e una band che accelera da
118 a 126 in dodici secondi ne muove 0.33 — il segnale sta sotto il rumore di un
fattore due, quindi nessuna soglia su di esso separa niente»*.

- [ ] **La forma della soluzione non è una soglia, è una contrazione.** Invece
  di decidere se estrapolare, **pesare** l'anticipo per il rapporto
  segnale/rumore: `lead × σ²signal/(σ²signal + σ²noise)`, con il rumore stimato
  da `shortFitRate` (che esiste già come diagnostico proprio perché fu lui a
  mostrare il muro). A tempo fermo il peso va a zero da solo e la precisione del
  fit lungo resta; su una deriva vera il peso sale. Non è stato provato.
#### CORRETTO: `unknown` serviva due popolazioni e dava a entrambe la risposta della prima

`unknown` non è un regime di passaggio. `mayFix` vuole `spread < 0.015`,
`moving` vuole `|trend| > 0.018`: **una band dal vivo cade nel mezzo** — lo
scatter umano è troppo largo per essere chiamata fissa, la deriva troppo lenta
dentro la finestra per essere chiamata in movimento. Ci resta, e ci vive.

Le due popolazioni si separano **senza nessuna soglia sul segnale** — che è il
muro documentato — perché si separano su **da quanto tempo siamo qui**. Chi
acquisisce ha pochi battiti in regime; chi ci vive ne ha centinaia. Sotto una
finestra lunga intera (`beatsInRegime > kLongFit`) resta il fit lungo e la sua
precisione; oltre, si applica lo stesso anticipo che il ramo `live` usa già.

| | base | dopo |
|---|---|---|
| **`probe_tempo_step` 120 → 150** | 12.8 s | **1.2 s** |
| **120 → 160** | 23.6 s | **1.1 s** |
| **100 → 160** | 10.1 s | **1.1 s** |
| **160 → 100** | 12.0 s | **1.8 s** |
| **120 → 90** | 10.7 s | **2.0 s** |
| **90 → 120** | 20.7 s | **2.2 s** |
| banco reale, ritardo medio | 6.83 s | **5.33 s** |
| banco reale, errore | 0.94% | 0.94% |
| banco reale, strattoni | 1.71% | 1.86% |
| `VPAlign` 100 → 140, tempo | 11.26 s | **1.30 s** |
| `VPAlign` 100 BPM jitter 2.2, scatti | 3 | **0** |
| `probe_matrix` uscite | 98 | 100 |
| `probe_matrix` fuori medio | 8.98% | 9.04% |

`probe_tempo_step` guadagna anche una riga: `violent non-octave changes: PASS`.
`--level` 15/1, `--octave` 6/5, `--bar` 10/0, `--tempo-slow` 10/0, `--swing`
3/0, tutte alla base.

**Il costo, e va guardato in faccia.** `VPAlign` segna **FAIL** sulla riga
`100 -> 140`: il tempo ci arriva 8.7 volte più in fretta (11.26 → 1.30 s) ma la
colonna `trans ok` passa da 4/4 a **0/4**. Il percorso ordinario adesso arriva
prima della macchina delle transizioni, che quindi non conferma più. La fase su
quella riga è **identica** (57.1 ms, `fase ok` 0/4 in entrambi), quindi il danno
misurabile è zero — ma la conseguenza architetturale è vera e va detta:

- [ ] **Il gate delle transizioni diventa in gran parte ridondante sui
  gradini**, perché il percorso ordinario è ora altrettanto rapido. Quel gate
  però ha protezioni che l'ordinario non ha (due intervalli coerenti, almeno il
  3%, il bordo), e arrivare in fretta senza quelle vuol dire seguire in fretta
  anche un gradino **falso**. Le due uscite in più di `probe_matrix` sono
  probabilmente quello. Da decidere: o si accetta, o l'anticipo va sospeso
  mentre una transizione è in valutazione.
- [ ] `probe_matrix`: uscite 98 → 100 e fuori medio +0.06. Il resto del banco
  reale non peggiora, quindi il baratto è responsività contro due time-out su
  360. Rimisurare se si tocca ancora.
- [ ] Il brano 6 non ha una curva di verità utilizzabile (2 punti buoni). Il
  banco è a cinque.

**Due tentativi ulteriori, misurati e scartati (2026-09-14).** Entrambi usavano
`moving` (il trend del fit lungo) per accelerare il rientro, e nessuno dei due
regge, per la stessa ragione per cui il muro qui sopra esiste: `moving` è troppo
rumoroso durante l'acquisizione e i transitori, quindi non separa "la band
deriva" da "il fit sta ancora assestandosi".
1. **Escludere il pettine quando `moving`** (`pullTowardsComb`): sulle rampe di
   `VPAlign` migliora solo la *media* di 1-4 ms (il peggio è identico), ma
   `probe_matrix` passa da **98 a 102 uscite**. Ripristinato.
2. **Committare a `kRateAcquiring` quando `moving`** (ramo `live`): la fase
   "dopo" la rampa migliora (13.3→9.9, 12.9→10.2 ms), ma `probe_matrix`
   aggancio medio **5.23 → 8.84 s** e uscite 98→103. Ripristinato.
Non riprovare in questa forma: il discriminatorio pulito è la verità di fase
(i tap), non un'altra soglia su `moving`/`trend`/`shortFitRate`.

---

### 39. Il rallentando: perché ci mette sei secondi, e perché «al primo colpo» non è possibile 🔴 (2026-09-11)

Segnalazione, ascoltando il live: *«se c'è un rallentando del batterista, tipo
di 4 bpm, ci impiega circa 6 secondi ad allinearsi, invece di adattarsi al primo
colpo di batteria successivo»*.

#### Prima: l'item 37 era sbagliato, e l'ho scoperto qui

Fixture nuova, il caso esatto: 90 BPM, rallentando musicale a 86 in quattro
secondi, poi fermo.

| taratura | tempo dichiarato | **griglia vera** |
|---|---|---|
| **low** (il default messo nell'item 37) | 13.4 s | **11.9 s** |
| medium | 3.1 s | **7.0 s** |
| high | 3.1 s | **7.0 s** |

**LOW è quattro volte peggio su un rallentando.** L'item 37 aveva misurato LOW
contro HIGH sulla precisione a regime e sui **gradini**, e aveva concluso che il
caveat era chiuso. Era sbagliato: **un gradino passa per la macchina delle
transizioni, un rallentando no** — deve essere seguito dall'anello ordinario,
che è dove vive il limite di piegatura. Il caveat non era chiuso, era stato
provato sul caso sbagliato.

E con la correzione dell'item 38 in piedi, anche il banco reale si rovescia:

| taratura | ritardo | errore | strattoni | rallentando |
|---|---|---|---|---|
| low | 5.33 s | 0.94% | **1.86%** | 11.9 s |
| medium | 5.12 s | 1.07% | 2.23% | 7.0 s |
| **high** | **4.12 s** | **0.94%** | 2.23% | **7.0 s** |

**Default ripristinato a HIGH.** L'unico costo è lo strattonamento (2.23 contro
1.86) e il peggior spostamento di fase sul brano 1 (364 contro 182 ms); su Sally
le due tarature sono pari (struttura 3.13 contro 3.12).

L'item 37 resta a documento **superato**: la sua misura era valida per quello
che misurava e insufficiente per decidere.

#### «Adattarsi al primo colpo» non è possibile, ed ecco il numero

A 90 BPM un intervallo dura 667 ms. Un rallentando di 4 BPM lo allunga del
**4.4%**. Lo scatter umano su un singolo intervallo:

| jitter | in percentuale dell'intervallo |
|---|---|
| 10 ms | 1.5% |
| 20 ms | **3.0%** |
| 30 ms | **4.5%** |

**Un intervallo solo non distingue il rallentando dal jitter del batterista.**
Servono due intervalli come minimo teorico e realisticamente tre o quattro per
esserne sicuri: a 90 BPM sono **2-3 secondi**. Quello è il pavimento fisico, e
non è un limite di implementazione.

Ma siamo a **7 secondi**, non a 2-3. C'è un fattore due o tre da recuperare, e
si sa dove.

#### La lacuna strutturale: non esiste un percorso rapido per una rampa

Il gate delle transizioni rapide vuole che **il primo intervallo cambiato
differisca di almeno il 3%** dal precedente (`kTransitionSmallestStep = 0.05`,
e la soglia del candidato non scende sotto quello). Un rallentando di 4 BPM
distribuito su quattro secondi — sei battiti — cambia **ogni intervallo dello
0.7%**. Non supera mai il gate.

Quindi: **un gradino ha un percorso rapido (3.1 battiti). Una rampa non ne ha
nessuno** e resta ai fit, 8-24 battiti. È per costruzione, non per difetto di
taratura, ed è esattamente il caso che l'utente sente.

#### Il rilevatore di rampa: costruito, misurato, scartato — e ha spostato il bersaglio

Costruito come previsto: `fastDriftBeats >= 3` con segno coerente, transizione
ferma, livello non provvisorio, e uno scarto sotto il 5.7% (oltre è un gradino o
un'ottava, che hanno i loro percorsi). Due forme provate, entrambe peggiori:

| | tempo dichiarato | griglia |
|---|---|---|
| base | **3.1 s** | **7.0 s** |
| rampa **sommata** al commit del regime | 12.5 s | 8.2 s |
| rampa che **sostituisce** il commit | 13.8 s | 8.0 s |

La prima forma è annullata dal ramo del regime, che subito dopo tira verso il
proprio fit — in ritardo durante una rampa — a `kRateAcquiring` (0.70). La
seconda è più lenta della cosa che sostituisce: la mediana di tre intervalli a
0.35 per battito **è più lenta** del fit estrapolato a 0.70. Rimosso.

**E i due numeri della base dicono perché era destinata a non servire.** Il
tempo *dichiarato* arriva in **3.1 s**, che è già dentro il pavimento fisico di
2-3 s calcolato sopra: **il decoder si accorge già quasi subito.** I **3.9 s che
restano sono nell'orologio**, non nel decoder — sono la fase accumulata durante
la rampa, che va ripagata attraverso la velocità e al rail costa tempo.

Dopo la correzione dell'item 38 il decoder ha già un percorso rapido per le
rampe: il ramo `unknown` estrapola in avanti a 0.70 per battito. Il rilevatore
di rampa arrivava a cose fatte.

#### Provato: dare al clock la fase insieme al tempo. Misurato no-op, e dice qual è il vero collo di bottiglia.

Il meccanismo esisteva già a metà: `phaseTau = tempoTransitionActive() ?
kGridTauRapid : gridPhaseTau(...)`. Quando una transizione è confermata la fase
viene presa quasi diretta (0.10 s invece di 0.90). Ma è legato al percorso dei
**gradini**, e una rampa non conferma mai una transizione.

Esteso a «il tempo si sta muovendo», misurato sul numero che il decoder ha già
deciso e non su evidenza rumorosa: un riferimento del tempo impegnato con
costante 1.5 s, e `refMoved > 0.008` come test.

**Scatta** — strumentato, 1908 blocchi su 23400 (8.2%), cioè tutta la rampa. E
non cambia niente:

| | tempo | griglia | sovraelongazione |
|---|---|---|---|
| base | 3.1 s | **7.0 s** | +18.7% |
| con la fase diretta | 3.1 s | **7.0 s** | +21.5% |

Rimosso, e la traccia è tornata identica alla base.

**Il perché è la cosa da portarsi via.** `setGridPhase` fissa un *bersaglio*:
`phaseTau` governa quanto in fretta il clock **stima** l'errore di fase.
`steerLim` / `steerCeil` governano quanto in fretta può **agire**. Stimare più
in fretta non serve se il rail è lo stesso — e infatti la sovraelongazione
peggiora, perché il clock arriva al rail prima e ci resta più a lungo.

**Siamo al pavimento del progetto.** L'unico modo di muovere la fase più in
fretta del rail è un salto, e i salti sono stati messi sotto tetto nell'item 35
proprio perché si sentono come strappi. Il rail esiste perché nessun colpo venga
mai suonato due volte o saltato.

- [ ] Resta una sola strada onesta, ed è **non accumulare** la fase invece di
  ripagarla: i 3.9 s nascono dai 3.1 s in cui il clock gira ancora sul tempo
  vecchio mentre la band è su quello nuovo. Ma i 3.1 s del decoder sono già
  dentro il pavimento fisico di 2-3 s (un intervallo solo non distingue un
  rallentando di 4 BPM dal jitter di 20-30 ms del batterista). **Il margine
  residuo è circa un secondo, non quattro.**
- [ ] Una cosa **non** provata e sicura in linea di principio: un cambio di
  *velocità* non salta né duplica nessun colpo — solo un salto di *fase* lo fa.
  Il clock fa scivolare anche il tempo (tau 0.22-0.28 s da agganciato).
  Prenderlo diretto quando il decoder lo muove in modo netto è lecito e
  toglierebbe qualche decimo di fase accumulata. Piccolo, ma è il solo pezzo
  rimasto che non costi uno strappo.
- [ ] Da rimisurare con `scripts/analysis/rall4.wav`, che è la fixture di questo
  caso ed è ripetibile.
- [ ] Da misurare contro `probe_tempo_step`, `probe_matrix` e la fixture
  `rall4.wav` insieme: un rilevatore di rampa troppo sensibile insegue il jitter,
  che è il difetto opposto e si sente di più.

### 40. Il calo di uno-due secondi: il decoder non lo vede, e la fiducia si chiude per sedici 🔴 (2026-09-11)

Segnalato dall'ascolto: «a volte c'è un calo di tempo di uno o due secondi e poi
una ripresa, e in quei casi si nota l'uscita delle percussioni». È una forma
diversa dal rallentando dell'item 39 e va misurata a parte.

**Fixture nuova.** `dip.wav`: 90 BPM, calo a 86 in un secondo, tenuto un secondo,
ripresa a 90 in un secondo, poi fermo. Cassa/rullo su 1-2-3-4, charleston sugli
ottavi. Generata da `scripts/analysis/makedip.py`; il wav non è versionato, come
`rall4.wav`.

**Quanto costa, misurato.** Picco di fase **+88 ms**, rientro sotto 15 ms tenuto
quattro secondi dopo **18.1 s**, sovraelongazione **-22 ms** dall'altra parte.
Contro i 7.0 s del rallentando: il calo breve costa più del doppio.

**Dove NON è il collo di bottiglia** (tre cose escluse per misura, non per teoria):

- **Non è la rotaia dello sterzo.** `|steer|` massimo 0.0154 contro un `lim` di
  0.050: l'anello sta al **31%** del suo permesso, e ci sta per tutti i dieci
  secondi. Lo 0% del tempo al binario.
- **Non è il tau della fase.** Vedi item 39: già provato ed è un no-op.
- **Non è il glide del tempo, e «prendere il tempo diretto» non ha niente da
  prendere.** Il decoder *non* fa scivolare il tempo su un cambio vero: su
  `rall4.wav` salta da 90.00 a 86.34 in un colpo e l'orologio lo prende con tau
  0.22 s. Su `dip.wav` non lo muove affatto — il tempo pubblicato va da 89.98 a
  89.81 attraverso un calo di 4 BPM. **L'idea rimasta dall'item 39 è chiusa:
  misurata, non produce nulla.**

**Dov'è davvero.** Due meccanismi, la stessa causa.

1. La fiducia (`EvidenceTrust`) va da **1.000 a 0.300** — il pavimento assoluto —
   nell'istante del calo, e ci resta **sedici secondi**: esattamente la finestra
   del fit lungo a 24 battiti a 90 BPM. Il residuo sale per un motivo
   **meccanico** (una retta fittata su beat il cui tempo si è piegato), non
   perché i colpi siano suonati male.
2. Con la fiducia al pavimento scattano due limiti scritti per un caso diverso
   (il batterista che smette di suonare): il tappo `kPoorLeanBeats` inchioda
   `phaseErrEma` a **0.0200** di battito mentre il bersaglio vero sta a 0.06, e
   il tau del glide del tempo passa da 0.22 a **2.50 s**.

Il commento in `TempoFollower.cpp` dice «al massimo della fiducia, che è tutto il
resto incluso un accelerando, `poor` è zero». **La misura lo smentisce**: su un
calo di due secondi la fiducia sta al pavimento per sedici.

**Quanto spesso succede dal vivo** (cinque estratti della serata, oltre i 25 s):

| brano | al pavimento | tau del tempo > 1 s |
|---|---|---|
| 1 | 27.8% | 42.9% |
| 2 | 49.6% | 62.4% |
| 3 | 40.5% | 49.8% |
| 4 | 18.7% | 42.9% |
| 5 | 33.1% | 59.3% |

Il meccanismo scritto per «il batterista ha smesso» è attivo per **metà** di
un'esecuzione in cui non ha mai smesso.

**Due tentativi, misurati e non spediti.**

*(a) La fiducia della fase dal fit corto* (8 battiti invece di 24, campi
`phaseResidual`/`phaseCoverage` sull'ipotesi). Meccanicamente funziona: il tappo
si apre dieci secondi prima (t=66 invece di t=76) e l'errore si chiude due
secondi prima. Ma l'anello risuona — la cappa faceva da smorzatore per caso — e
su materiale vero il ritardo **peggiora su tutti e quattro** i brani con
correlazione attendibile: 7.75→9.75, 4.00→6.25, 1.50→5.75, 3.25→3.50 s. Media
+4.12→+5.80 s. Revertito.

*(b) Il pavimento solo sotto i 2.5 BPM d'errore* (la banda che il commento stesso
indica come «la patologia»). **No-op**: `rall4.wav` e `dip.wav` byte a byte
identici, perché `err` istantaneo è sempre piccolo — il decoder consegna un
gradino, non una rampa, e dopo il gradino `err` è di nuovo zero. Revertito.

**Misura di delimitazione: spegnere del tutto la risposta «evidenza scarsa».**
Non è un candidato da spedire, serve a sapere se il meccanismo si guadagna il
posto. Interruttore `VP_NO_POOR` (solo per il banco).

| | ritardo | errore | strattoni |
|---|---|---|---|
| base | +4.12 s | 0.94% | 2.23% |
| senza | **+3.62 s** | 1.00% | **1.95%** |

Meglio su tutti e quattro i brani misurabili (7.75→5.50, 4.00→3.50, 3.25→2.50,
3.75→3.00).

**E la misura di fase dice il contrario, che è la risposta.** `prec.py` sugli
stessi cinque estratti — «struttura» è picco/media dell'energia degli attacchi
piegata sulla fase dell'orologio: sopra 2 la griglia è sulla musica.

| brano | struttura base → senza | spost. peggiore |
|---|---|---|
| 1 | 3.11 → **3.04** | 182.6 → **334.7** ms |
| 2 | 3.37 → **3.26** | 236.6 → **260.2** ms |
| 3 | 3.40 → **3.27** | 285.9 → 239.5 ms |
| 4 | 2.96 → 3.04 | 102.5 → **161.1** ms |
| 5 | 3.87 → **3.75** | 98.6 → 56.3 ms |

Struttura peggiore su **quattro brani su cinque**. Gli strattoni migliorano ovunque, ma è
esattamente ciò che il meccanismo scambia. **Il guadagno di `bench_live` era
l'artefatto del ritardo-di-tempo, come previsto: il meccanismo si guadagna il
posto e resta.** `VP_NO_POOR` è stato tolto: la delimitazione è conclusa.

**Ma le due misure dicono cose opposte, e si sa perché.** `bench_live` misura il
**ritardo del tempo**, che quel meccanismo aumenta *di proposito*. `VPAlign`
misura la **fase**, ed è quella che si sente. Le sue righe (il confronto è già
dentro `VPAlign`: la riga «prima» *è* la configurazione senza risposta):

| | buco media | buco peggio | accel media | accel peggio |
|---|---|---|---|---|
| senza risposta | 21.4 | 39.9 | 18.4 | 41.2 |
| con (spedita) | 24.2 | 38.4 | **15.5** | **34.5** |
| buco con 44 ms di ritardo iniettato — senza | 28.1 | 72.2 | | |
| — con | **25.2** | **64.2** | | |

Quindi la risposta si guadagna il posto sull'**accelerando** (-16% medio e
-16% al peggio) e sul buco patologico, e la paga sul buco ordinario e sul
ritardo del tempo. **Non togliere il meccanismo sulla forza di `bench_live` da
solo**: è precisamente la trappola scritta in `scripts/analysis/README.md`
(«il BPM pubblicato non basta e inganna»).

**Dove si arena, detto chiaro.** Da dentro l'anello, dopo che il calo è finito,
un errore di 88 ms fermo e monosegno è **indistinguibile** da un *lean* di un
passaggio senza batteria (documentato a 44 ms, e a 90 BPM 88 ms sono 0.13 di
battito: stessa taglia, stessa forma, stesso segno). I due vanno chiusi in modo
opposto. L'unico punto in cui differiscono è **durante** il calo, dove il
bersaglio si muove — e lì durano due secondi, cioè sotto il pavimento fisico dei
2-3 s già misurato nell'item 39.

- [ ] Il discriminante, se esiste, non è nel follower: va cercato in cosa
  distingue un *tempo che si piega* da *beat suonati male* prima che il residuo
  del fit lungo li confonda. Candidato non provato: il segno e la persistenza
  dello scarto fra fit corto e fit lungo, che su un calo si apre e su un lean no.
- [x] Rimisurare qualunque candidato su **entrambi** i banchi, e sulla fase
  (`prec.py`) non solo sul tempo. **Fatto l'11/09/2026**, sotto.
- [x] La diagnosi iniziale non aveva ancora un cambio di codice; la chiusura
  misurata che segue risolve la memoria falsa senza rimuovere la protezione.

**Chiusura della memoria falsa, 11/09/2026.** Il discriminante utile non è il
BPM corto contro quello lungo: anche l'ingresso e l'uscita da un passaggio senza
batteria producono i due lobi opposti. È la *qualità* dei due fit. Dopo il calo
90→86→90 il residuo lungo resta 0.030–0.037 mentre quello sugli ultimi otto beat
è già tornato 0.003–0.005; nel passaggio senza batteria con 44 ms di ritardo i
due restano invece entrambi cattivi (0.054/0.046, 0.054/0.049, 0.054/0.052).

`EvidenceTrust` ora lascia decadere la memoria del fit lungo soltanto quando il
fit recente è contemporaneamente buono rispetto alla baseline del brano e fra
il 25% e il 50% migliore del lungo. L'autorità è continua, non un latch. Il fit
corto non sostituisce tempo o fase e non può saltare o duplicare colpi; risponde
solo alla domanda «i beat recenti sono di nuovo coerenti?». I valori sono
pubblicati nello snapshot come diagnostica e `VPTests --evidence` blocca i due
casi opposti.

Misura A/B con lo stesso audio, la stessa rete e `score_dip.py`: rientro entro
15 ms tenuto quattro secondi **12.1→9.5 s**; picco invariato +105.9 ms; minimo
-5.5→-14.3 ms, ancora dentro la banda di 15 ms. `VPAlign`, otto semi: il buco
senza batteria migliora 24.1→22.5 ms medio con peggiore invariato 39.2; la rampa
resta entro un millisecondo dal riferimento nel percorso completo.

Ultimi 5:07 della serata Flamingo, catena completa, 20–300 s: restart invariati
a uno (3.3 s); spostamento mediano della fase fra finestre **39→26 ms** e
strattoni 2.15→2.02% rms. La struttura degli attacchi 3.17→3.13 è sostanzialmente
invariata. Il massimo a finestre da 10 s 156→208 ms non è certificabile come
fase assoluta: in quella finestra l'istogramma cambia accento musicale; a 20 s
la stessa zona passa 126→102 ms e resta debole. Non usare questo dato come
verità senza una beat-grid annotata.

**Verifica indipendente della chiusura, 11/09/2026.** La chiusura sopra era
misurata su `VPAlign`, sul dip e su un tratto nuovo del Flamingo, ma non sui
cinque estratti di `bench_live` né con `prec.py` — cioè proprio il banco che
aveva bocciato il tentativo (a). Rifatto l'A/B completo `c21e6b1` → `2aa1653`,
stesso `build-host`, revert del solo `Source/`+`Tests/` e ripristino:

| | prima | dopo |
|---|---|---|
| `VPTests --level` | 15/1 | 15/1 |
| `VPTests --octave` | 6/5 | **7/4** (stabile su due corse) |
| `--bar` / `--tempo-slow` / `--swing` | 10/0, 10/0, 3/0 | identici |
| `VPTests --evidence` | — | 2/0 |
| `probe_recovery` | 90 PASS / 0 FAIL | identico |
| `probe_recovery --slow-passages` | 90 / 18 noti | identico |
| `probe_tempo_step` | PASS | PASS |
| `probe_matrix` aggancio / uscite / fuori | 5.24 s / 100 / 9.04% | 5.23 s / **98** / 9.08% |
| `bench_live` ritardo / errore / strattoni | +4.12 s (4/5) / 0.94% / 2.23% | +4.20 s (5/5) / 0.94% / **2.01%** |
| `score_dip.py` rientro | 12.1 s | **9.5 s** |

`prec.py` sui cinque estratti, spostamento **mediano** della fase fra finestre:
45.7→30.4, 71.0→59.2, 71.5→36.0, 22.0→22.0, 49.3→42.2 ms — **meglio o uguale
su tutti e cinque**, media 51.9→38.0. La struttura scende leggermente su quattro
su cinque (3.11→3.06, 3.37→3.22, 3.40→3.01, 2.96→3.09, 3.87→3.60): è il costo, ed
è molto minore di quello del tentativo (a). **Nessuna regressione; `--octave` e
le uscite di `probe_matrix` migliorano.** La chiusura regge.

**Trappola di misura scoperta qui, non rifarla.** Un A/B fatto costruendo il
commit precedente in un `git worktree` con un `cmake` fresco **dà numeri falsi**:
lo *stesso* commit `2aa1653` misura 0.94% di errore e 2.01% di strattoni in
`build-host` e **5.21% / 6.76%** nel worktree, riproducibile byte a byte in
entrambi. Non è cache fredda e non è il modello (stesso sha1): è la
configurazione del build. Un confronto su `bench_live` vale **solo** dentro lo
stesso `build-host`; per il «prima» usare `git checkout <sha> -- Source/ Tests/`
e ricostruire lì, poi `git checkout HEAD -- Source/ Tests/ scripts/`.

Tentativi esclusi nella stessa sessione, per non rifarli: compensare anche il
ritardo del commit IIR nel lead del decoder ha portato il peggiore reale
155.9→285.8 ms; una fase da fit a quattro beat non ha dato un vantaggio coerente
su `VPAlign`; rendere `live` appiccicoso ha peggiorato i tempi fissi. Tutti e tre
sono stati rimossi.



---

### 41. La causa dell'uscita dopo un'inflessione: la finestra a 24 battiti che la contiene ancora 🟡 (2026-09-11)

Richiesta: «se il tempo rallenta o si velocizza, la ripresa dell'app sul tempo
deve essere immediata... è ancora lenta».

**Dove se ne va il tempo, misurato sull'item 40 dopo la correzione di ChatGPT.**
Strumentando l'anello di fase sul calo (`VP_LOOP_LOG`, poi rimosso):

- **Non è la rotaia dello sterzo.** `|steer|` sta al 21-40% del suo `lim`, mai al
  binario, per tutta la correzione.
- **Non è il tappo `kPoorLeanBeats`.** Tolto da solo (interruttore `VP_NO_LEAN`,
  poi rimosso): `ema` 0.0199 → 0.0249, rientro **invariato**.
- **Non è lo smorzamento**, ma quello è un difetto vero e separato, sotto.
- **L'anello esegue correttamente un ordine sbagliato.** A t=65.7 l'errore vero è
  **+80 ms** e il bersaglio che il decoder gli dà ne dichiara **+10**; a t=69.8 il
  decoder dice 0.6 ms mentre la verità è +44.6.

**La causa.** Il regime è **FISSO**, dove *sia* il tempo (`fixedAnchorBpm`) *sia*
l'ancora di fase (`gridAnchorSec = longAnchor`) vengono dal fit a **24 battiti** —
sedici secondi a 90 BPM. Un calo di due secondi ci resta dentro per tutti e
sedici. Dalla traccia, otto secondi dopo che il tempo vero era tornato a 90.00:

| | corto (8 battiti) | lungo (24) |
|---|---|---|
| BPM | 90.00 | **89.26** |
| residuo | 0.004 | **0.030** |

e il tempo pubblicato segue quello lungo giù fino a **89.61**. La griglia gira a
**89.10** di media per dodici secondi e fabbrica **120 ms** di errore di fase che
nessun anello di sterzo può rifiutare. È questo che si sente: le percussioni
escono **dopo** l'inflessione, non durante.

**La correzione.** `longWindowStraddles`: le due finestre lo dicono da sole quando
una sta a cavallo di un evento — non sono d'accordo sul tempo **e** la corta fitta
molto meglio. Entrambe le metà servono: su materiale fermo i residui da soli si
separano di tre a uno con i due fit dentro un centesimo di BPM. Un passaggio senza
batteria fallisce la metà del residuo per costruzione (lì anche il fit corto è
cattivo: 0.046-0.052 contro 0.054), che è la stessa separazione su cui è costruito
`shortFitResidual` di ChatGPT — questa la riusa.

L'ancora di fase **sfuma** fra le due in mezzo secondo, non commuta. Commutare è
un *gradino* nel bersaglio pubblicato, e `BeatTracker` si stacca e riaggancia sopra
un terzo di battito: preso come interruttore è costato strattoni sul brano 1
(2.40 → 2.73% rms) e sul 3 (2.65 → **4.66%**, peggiore 15.2 → **44.8%**) — cioè lo
strappo che questo serve a togliere, rimesso dalla sua stessa rimozione. Provata
anche l'isteresi (rilascio tre volte più lento): peggiore dello sfumato simmetrico
su entrambi i brani, scartata.

**Misure.** Calo (`makedip.py`), contro l'allineamento proprio dell'app:

| | rientro | sovraelongazione | residuo permanente | jitter a regime |
|---|---|---|---|---|
| prima | 18.0 s | −31.7 ms | **+10.0 ms** | 1.5 ms |
| dopo | **16.2 s** | **−23.7 ms** | **+1.9 ms** | 1.5 ms |

Lo spostamento *permanente* che un calo lasciava è praticamente sparito.

Cinque estratti live, `prec.py`:

| brano | struttura | spost. peggiore | strattoni rms | strattoni peggio |
|---|---|---|---|---|
| 1 | 3.06 → **3.25** | 182.5 → **121.7** | 2.40 → **2.37** | 16.4 → **14.9** |
| 2 | 3.22 → **3.26** | 236.6 → 236.7 | 1.52 → **2.40** | 11.9 → **15.2** |
| 3 | 3.01 → **3.17** | 264.0 → **48.0** | 2.65 → **2.56** | 15.2 → 15.3 |
| 4 | invariato | invariato | invariato | invariato |
| 5 | invariato | invariato | invariato | invariato |

Il brano 3 è quello segnalato come «tiene malissimo il tempo, esce spesso»: il suo
spostamento di fase **peggiore** passa da 264 a 48 ms. **Il brano 2 peggiora sugli
strattoni** ed è il costo dichiarato di questo cambio.

Regressioni, tutte al riferimento: `probe_matrix` 5.23 s / 98 uscite / 9.08%
(identico), `probe_tempo_step` PASS, `VPTests --level` 15/1, `--bar` 10/0,
`--tempo-slow` 10/0, `--swing` 3/0, `--octave` 7/4, `--evidence` 2/0.
`bench_live` +4.20 → +4.05 s, errore 0.94% invariato, strattoni 2.01 → 2.15%.

**Difetto separato, trovato e NON spedito.** Il termine derivativo dell'anello di
fase è una differenza **per blocco**, non una velocità:
`dErr = wrapCentered (e - prevPhaseErr)`. A 256 campioni vale 5.8e-5 contro un
termine proporzionale di 0.020 — lo **0.35%** del comando — e quattro volte tanto a
1024, quindi lo smorzamento dipende silenziosamente dalla dimensione del buffer
dell'host e sul banco non c'è mai stato. Reso una velocità (`/dt`, con `dGain` che
diventa secondi di anticipo contro il ritardo della media) migliora la
sovraelongazione (−31.7 → −35.8 sul picco ma residuo permanente 10.0 → 1.4 ms) e
**raddoppia il jitter di fase a regime, 1.5 → 3.7 ms rms**: differenzia il rumore.
Revertito. Va ripreso con un limite di banda sulla derivata, non così.

- [ ] «Immediata» **non è raggiunta**: 16.2 s contro 18.0. Il guadagno grosso
  (8.3 s) esiste ed è la variante a commutazione secca, che costa lo strappo. Il
  pezzo che manca è dare al decoder una fase che non venga da una finestra a
  cavallo, **senza** cambiare sorgente — cioè rifittare l'ancora escludendo i
  battiti dentro l'evento, invece di scegliere fra due fit.
- [ ] Il costo sul brano 2 va capito prima di considerare chiuso l'item.
- [x] `VPAlign` rimisurato l'11/09/2026, e **dice di no sulle rampe**. Sotto.

**Verifica su `VPAlign`, 11/09/2026: la rampa peggiora.** Confronto `2aa1653` →
`4fead68` nello stesso `build-host`, `VPAlign` deterministico (due corse a HEAD
identiche riga per riga). Fase peggiore contro la griglia scritta:

| | prima | dopo |
|---|---|---|
| `100 -> 110 in 30 s` | 151.7 ms | 151.7 (invariato) |
| `100 -> 110 in 12 s` | 165.9 ms | **193.8** |
| `120 -> 132 in 20 s` | 145.9 ms | **236.8** |
| `128 -> 120 in 20 s` | 73.7 ms | 73.7 (invariato) |

Le tabelle dell'orologio - buco senza batteria e accelerando - sono **identiche**
(differenze di 0.1 ms, cioè il rumore della misura): la protezione regge. È la
rampa che paga, e un'accelerazione graduale è la cosa più comune che faccia un
batterista dal vivo.

**Il meccanismo.** Su una rampa i due fit sono in disaccordo *continuamente* e il
fit corto fitta davvero meglio di una retta a ventiquattro battiti una curva, così
entrambe le metà del test restano vere e l'ancora scivola verso il fit più
rumoroso, avanti e indietro.

**Tentativo fallito, per non rifarlo.** Limitare la durata dell'aggancio a una
finestra lunga, con pari attesa prima di riagganciare: **no-op** (58.1 e 39.3
invariati). Il danno non viene da un aggancio lungo ma da accensioni ripetute e
brevi dentro la rampa. Rimosso.

**Il bilancio, per decidere.** A favore: il calo rientra in 16.2 s invece di 18.0,
la sovraelongazione scende da −31.7 a −23.7 ms, lo spostamento *permanente* che un
calo lasciava passa da +10.0 a +1.9 ms, e sul materiale vero il brano 3 passa da
264 a **48 ms** di spostamento di fase peggiore. Contro: la rampa da 20 s passa da
146 a 237 ms di fase peggiore, e il brano 2 peggiora sugli strattoni.

- [x] **Decisione aperta — CORRETTA (2026-09-14), con un discriminante diverso da
  quello previsto.** La raccomandazione era di non tenerlo così: 237 ms su una
  rampa sono un quarto di secondo di percussioni fuori, sul caso live più
  frequente. L'ipotesi scritta («fit corto vicino alla baseline del brano») è
  stata **provata e falsificata**: su `VPAlign` la rampa è troppo pulita perché
  il residuo del fit corto si stacchi dalla baseline, quindi quella condizione è
  un no-op lì (e il banco deterministico non cambia di un millesimo). Il
  discriminante che funziona è più semplice: lo straddle deve scattare solo
  quando il fit corto **concorda col tempo già commesso** (`shortAgreesCommitted`,
  `kStraddleAgreeRatio = 0.008`). Su un calo il corto torna a ~0.1% del
  commesso; all'inizio di una rampa è già a 1.6% e ci resta, perché il decoder è
  ancora `fixed` e il `moving` non ha ancora visto la rampa — ed è proprio
  quella finestra in cui lo straddle mescolava l'ancora col fit corto rumoroso.
  Misure:
  - `VPAlign` rampa: `120→132 in 20 s` peggio **236.8 → 145.9 ms**, `100→110 in
    12 s` **193.8 → 165.9 ms** — tornati ai valori pre-straddle. `100→110 in 30 s`
    e `128→120 in 20 s` invariati. Gradini tutti PASS (salvo il `100→140`
    preesistente), buco/accelerando identici al riferimento.
  - Dip (`makedip`): rientro ≤15 ms tenuto 4 s **9.6 s** — invariato, il calo
    resta corretto.
  - `probe_tempo_step` identico riga per riga, `probe_matrix` **identico**
    (5.23 s / 98 uscite / 9.08%), gate `--bar`/`--tempo-slow`/`--swing`/
    `--evidence`/`--new-input`/`--rhythm`/`--state-timing`/`--tempo-step` verdi.
  - Materiale reale (5 brani Flamingo): nessuna regressione; strattoni del brano
    1 **3.58 → 2.49%** e spostamento di fase del brano 5 **268 → 169 ms**.

### 42. Il vocoder di fase azzerato a ogni callback: un terzo della CPU 🟢 (2026-09-11)

Domanda: si puo' ridurre il consumo di batteria durante l'uso.

**Come si misura.** `VPCpu <blocco> [mic]`. Senza argomenti misura il percorso
*speaker* (cancellatore di rientro a finestra larga); con `mic` misura il
microfono sul kit o la mandata dal banco, che e' quello che una band usa dal
vivo. Il secondo argomento e' stato aggiunto qui: prima il banco misurava solo
il caso peggiore, che non e' quello dell'utente.

**Il reperto.** Il consumo scalava quasi inversamente con la dimensione del
buffer - 7.2% di un core a 128 campioni contro 1.7% a 1024 - e un fit lineare
sui quattro punti isola **177 us di costo fisso per callback**, cioe' il **69%**
di tutto quello che l'app spendeva al buffer comune da 256.

Campionando il processo (`sample`), 169 campioni su 349 dentro `processBlock`
stavano in `HybridPercussionRenderer::render` -> `LoopPlayer::stop()` ->
`LoopStretcher::reset()` -> `DynamicSTFT::reset` (bzero dei buffer, riassegnazione
di vettori, `addWindowProduct`).

La causa e' una riga in coda a `render`:

```cpp
if (mode == Mode::strokes && blend <= 0.0f) { congas.stop(); shaker.stop(); }
```

In PATTERN senza registrazioni miscelate - il caso ordinario - quella condizione
e' vera **a ogni blocco**, e `stop()` azzerava un vocoder di fase per voce.
L'app resettava due vocoder cinquemila volte al minuto per spegnere qualcosa che
era gia' spento.

**La correzione.** In `LoopPlayer::stop()` ogni scalare continua a essere
azzerato incondizionatamente - lo stato lasciato e' identico bit per bit - e solo
`v.stretcher.reset()` viene saltato per una voce gia' inerte. E' equivalente per
costruzione: `advance` esce su `! v.active || v.index < 0` prima di arrivare a
`stretcher.process`, quindi il vocoder di una voce spenta non puo' essere stato
toccato dal suo ultimo azzeramento.

**Misure**, 30 s di audio, percorso microfono/mandata:

| buffer | prima | dopo |
|---|---|---|
| 128 | 7.2% di un core | **3.9%** |
| 256 | 4.2% | **2.7%** |
| 512 | 2.4% | 1.9% |
| 1024 | 1.7% | 1.5% |

Percorso speaker, 256: 4.8% -> 3.6%.

Regressioni: `VPTests --loops` 58/0 identico, `--leak` 47/2 e `--makeup` 83/9
**identici a HEAD** (quei fallimenti sono preesistenti e non c'entrano con
questo). Nessun percorso del tempo e' toccato.

**Cosa resta, misurato e non fatto.**

- Dopo la correzione restano ~108 us fissi per callback. Il profilo li mette in
  `subtractSpeakerLeak` -> `updateLeakDelay` (66% del callback sul percorso
  speaker) e in `HarmonicChange::analyseWindow` (21%).
- [ ] **`leakScanCountdown` conta blocchi, non tempo**:
  `speaker ? (locked ? 4 : 1) : 8`. La ricerca grossolana usa
  `step = numSamples / 8`, quindi costa lo stesso (~30k operazioni) a qualunque
  buffer, ma riparte ogni N *blocchi* - otto volte piu' spesso al secondo a 128
  campioni che a 1024. Il ritardo acustico non cambia piu' in fretta con un
  buffer piu' piccolo, quindi e' un'incoerenza prima ancora che un costo: la
  velocita' di adattamento del cancellatore dipende oggi dal buffer dell'host.
  Renderlo a tempo e' la correzione giusta, ma va scelto il riferimento e
  **misurato con `VPTests --leak`** (che ha 2 FAIL noti da prima): fissarlo sul
  comportamento a 256 non da' nessun guadagno al buffer piu' comune e ne toglie
  a 1024. Non fatto qui perche' il guadagno certo era altrove.
- [ ] Il buffer e' la leva piu' grande che resta e non e' nel codice: fra 128 e
  1024 campioni ci sono 2.6 volte di consumo. Se l'app puo' chiedere all'host un
  buffer piu' grande senza costare latenza percepita, vale piu' di qualunque
  micro-ottimizzazione.

### 43. Il pettine vecchio annullava un cambio gia' confermato 🟢 (2026-09-15)

Sul percorso file/mixer il cambio era riconosciuto presto ma non restava preso:
120→132 veniva confermato a +0,92 s, poi il pettine ancora a 120 riportava la
pubblicazione successiva a 129,2 BPM e il clock si stabilizzava solo a 8,90 s.
Su 120→108 il rimbalzo arrivava dopo gli otto battiti del fit corto e spostava la
stabilita' a 14,12 s.

Durante la finestra limitata di refit, solo su `lineFeed`, il live target ora
ignora il pettine per 8+3 battiti accettati. Dopo: 120→132 **0,92 s al BPM / 1,34
s stabile**, 120→108 **1,14 / 1,66 s**; errore massimo dopo conferma 0,041 e
0,026 BPM. Nessun restart o arretramento del clock. Il microfono iPad non cambia.

Gate: nuova regressione `VPTests --tempo-step` 11/11; `probe_matrix` completo
identico a HEAD (5,22 s / 104 uscite / 30 mai / 9,11%); cinque dump impulsi
Flamingo identici byte per byte; `VPAlign` sei gradini + due rampe PASS, incluso
100→140 in 1,30 s e 24,5 ms un battito dopo l'evidenza; `probe_recovery` 84/84.
Suite generale 620/27, con rossi storici fuori dalla transizione; non A/B completa.
Dettaglio e limiti rimasti in `docs/HANDOFF_TEMPO.md`.

Follow-up rampe dello stesso giorno: il confronto sopra resta il checkpoint del
solo gradino. Il nuovo gate di fase `VPAlign --ramps` ha poi ridotto il peggio
MIXER 140,7→84,7 ms (100→110/30 s), 153,4→127,2 (100→110/12 s),
130,4→95,5 (120→132/20 s) e 62,7→48,0 (128→120/20 s), lasciando invariati i
controlli fissi e il buco batteria. Con questo follow-up la matrice conserva
5,22 s / 104 / 30 ma il fuori passa 9,11→9,12%; quattro dump Flamingo cambiano
e il suo riferimento debole passa 8,62→8,70% errore, 2,21→2,27% strattoni.
Non chiamare quindi il follow-up byte-identico o la fase live gia' certificata.

### 44. Curvatura su rampa diretta: diagnostica, rilascio respinto 🟡 (2026-09-16)

Il debito rimasto nasceva prima che il fit corto accumulasse abbastanza scarto:
sulla rampa 100→110/12 s il vero tempo era gia' 104,17, il decoder ancora
100,07 e il clock 69,9 ms tardi. Una quadratica a otto battiti e' inutilizzabile
(fino a ±1,7 BPM/battito anche a 100 fisso). Il fit curvo su sedici battiti
resta nel tree, ma non guida BPM/fase e **non libera FISSO**.

Il primo checkpoint aveva documentato come attivo un rilascio che non era
collegato allo switch. `VPAlign --ramps` fresco ha infatti riprodotto i numeri
precedenti: 22,0/84,7; 40,8/127,2; 28,5/95,5; 20,2/48,0 ms MIXER.

`probe_motion_matrix` ora misura il vero percorso MIXER mentre suona, senza snap
da STOP, su tre popolazioni casuali con verita' nota. Il vecchio selettore a tre
curve produce **8 falsi su 128 tempi fissi**, 51 prove sul moto continuo e 11
sui gradini: non e' globale. Il requisito di migliorare la retta del 50% elimina
i falsi ma arriva troppo tardi; usarlo solo come innesco non basta; quattro
prove non filtrate conservano ancora un falso. Tutte e tre le varianti di
produzione sono state rimosse.

Il diagnostico corrente richiede tre curve che dimezzino l'errore quadratico
della retta. Matrice 64 casi/famiglia: prove 0/11/3 su fisso/continuo/gradino;
fase media MIXER 18,9/64,4/52,5 ms e p95 50,1/149,3/218,2 ms. Sono baseline
contenenti anche casi d'ottava ambigui, non una certificazione.

- [x] banco globale con beat-grid vera e gate anti-falso sul selettore;
- [x] documentazione corretta: nessun rilascio da curvatura in produzione;
- [x] progettare autorita' di moto continua e limitata, non un altro switch (item 45);
- [ ] beat-grid manuale su almeno due tratti con accelerando/rallentando;
- [ ] ascolto umano della parte renderizzata contro quei tratti.

### 45. Riallineamento sui cambi di tempo: candidati respinti, percorso sicuro ripristinato 🔴 (2026-09-17)

Sia il ponte ibrido del piano Codex sia il successivo filtro IMM sulle date dei
battiti sono stati respinti e rimossi. Il filtro migliorava alcune rampe
sintetiche, ma non superava il contratto globale: ai quattro offset rapidi
0/16/32/48 cambiava gli hash di **tutti** i casi fissi e a gradino. Inoltre il
primo confronto con tap umani peggiorava tutte le metriche. Non e' quindi un
percorso produttivo valido, anche se non conteneva eccezioni esplicite per quel
brano.

Dopo la rimozione, `probe_motion_matrix` torna bit-identico al controllo su
fisso e su tutti i gradini. Le righe continue restano volutamente rosse nel
comparatore (autorita' zero e nessun miglioramento): e' la prova che la
diagnostica non viene scambiata per una soluzione. `VPAlign --ramps` riproduce
la baseline: 22,0/84,7; 40,8/127,2; 28,5/95,5; 20,2/48,0 ms MIXER. Il problema
del riallineamento continuo resta aperto.

- [x] respingere e rimuovere il ponte ibrido che alterava i gradini;
- [x] respingere e rimuovere il filtro IMM che alterava fisso/gradini;
- [x] conservare classificatore, diagnostica e gate anti-vacuita';
- [x] suite mirate verdi dopo il ripristino (`--tempo-motion` 289/0 e gli altri gruppi 0 fail);
- [ ] progettare un nuovo candidato **globale**, senza titoli, seed, offset o fasce BPM speciali;
- [ ] ottenere autorita' positiva e media/p95 strettamente migliori su ogni riga continua;
- [ ] mantenere hash identici e autorita' zero su fisso e su tutti i gradini;
- [ ] superare `VPAlign`, gate indipendenti e suite completa;
- [ ] validare almeno due beat-grid reali e ascoltare accelerando/rallentando su mixer e file;
- [ ] affrontare il microfono iPad solo dopo la chiusura del percorso diretto.

**Tap umani su `26 SPLENDIDA GIORNATA` (2026-09-17).** 117 quarti battuti
dall'utente fra 10,8 e 75,4 s (~107,8 BPM), griglia lisciata con quadratica
locale; motore completo `VPTrack --pulses`, offset costante tolto. Errore del
clock, media / p95 / quota >50 ms:

| riferimento | percorso ripristinato | filtro respinto | filtro + ritorno solo su cassa (revertito) |
|---|---|---|---|
| tap lisciati +/-4 | **27,5 / 72,6 / 13,0%** | 31,3 / 76,9 / 14,3% | 32,9 / 77,2 / 14,5% |
| tap lisciati +/-8 | **26,2 / 67,2 / 11,2%** | 30,3 / 72,7 / 16,6% | 31,9 / 74,8 / 18,3% |
| offset tolto ogni 10 s | **16,6 / 43,7 / 3,2%** | 18,6 / 71,4 / 7,9% | 21,1 / 74,8 / 9,1% |

A 32-36 s i tap restano a 108,5-109 BPM mentre gli attacchi dell'audio (beat
tracker offline non causale) arrivano come se la band rallentasse a ~106,5: il
filtro segue gli attacchi, l'orecchio no. Il commit `ec68386`, tarato sul
riferimento offline, e' stato quindi revertito (`8e08590`). Lezioni:

- un riferimento costruito sugli attacchi non e' verita' di fase: su questo
  brano si scosta dai tap di 30 ms medi (p95 65), piu' della differenza fra le
  versioni;
- `refine_taps.py` su questo brano peggiora i tap (jitter 30 -> 92 ms, spostamenti
  al bordo della finestra -120 ms): niente cassa netta su ogni quarto;
- `tap_recorder.py` ha misurato ~200-220 ms di ritardo costante contro sia
  l'app sia il riferimento offline: probabile avvio di `afplay` non contato.

- [x] rimuovere il filtro: i gate globali e il primo confronto reale non ne
      consentono l'uso; i brani reali restano verifiche, mai condizioni nel DSP;
- [ ] `tap_recorder.py`: misurare/compensare la latenza di avvio di `afplay`.

### 46. Cambio brusco di tempo riconosciuto quarto per quarto 🟡 (2026-09-17, codice + banchi ok, non committato — resta ascolto)

Segnalazione: *«se c'è un rallentamento o velocizzazione brusca, ci impiega
qualche secondo ad arrivare al tempo corretto»*.

**Causa misurata** (decoder + clock, battiti sintetici): il rilevatore a
intervalli confronta intervalli consecutivi, quindi il suo rumore è il doppio
dell'imprecisione dei colpi. Con 6 ms di imprecisione si spegne del tutto
(`3 * jitter > 5%`): un +10% a 120 BPM non apriva nemmeno un candidato (~4 s).
E sotto il 5% non interviene mai: gradini puliti del 3-5% costavano 8-13 s.

**Fatto:** `BeatDecoder::observeGridStep`, solo mixer/brano caricato. Dopo ogni
battito accettato prolunga la retta dei battiti prima di un perno e guarda gli
ultimi 2-4 quarti: un cambio vero se ne allontana di `k * passo`, un batterista
che si sposta di una costante. Conferma al primo m che lo prova contro
spostamento, colpo isolato, rampa, sedicesimi/fantasmi e quasi-ottava (dettagli
e numeri nella skill `realtime-tempo`, §2). Consegna al clock con la stessa
transizione `rapid` già misurata: niente snap, niente impulsi saltati.

| banco | prima | dopo |
|---|---|---|
| gradini ±3..15%, 70-150 BPM, puliti: tempo a stabile | 5,4 s | **1,9 s** |
| stessi, 6 ms di imprecisione | 12,1 s | **5,6 s** |
| stessi, 12 ms | 14,3 s | **11,5 s** |
| `VPTests --tempo-step` gradini con 6 ms (12 casi) | 4/12 confermati | **12/12 in 0,92-1,40 s** |
| `probe_motion_matrix --quick` fisso, 4 offset | — | **hash identici** |
| idem gradini, p95 ms | 180/209/196/165 | **175/174/171/139** |
| idem continuo, p95 ms | 132/109/87/133 | **130/105/82/127** |

Invariati: `probe_tempo_step`, `VPAlign --ramps`, `--tempo-slow` 10/0,
`--tempo-motion` 291/2 e `--octave` 5/6 (gli stessi rossi di HEAD). `probe_small_steps`:
52->50 passa da FAIL a PASS. Costo noto: all'inizio di rampe molto ripide (10%
in 12 s a 60-80 BPM) scatta; l'errore medio migliora, ma a 100 BPM il tempo
fuori 2,5% sale da 5,1 a 6,7%. Non coperti: gradini oltre ~15% con
imprecisione (i nuovi battiti escono dalla griglia) e il microfono iPad.

- [x] misurare dove si perdono i secondi (soglia di instabilità e buco 3-5%);
- [x] rilevatore quarto per quarto con i veti (spostamento, rampa, fantasmi, ottava);
- [x] test `--tempo-step` (gradini con imprecisione + spostamento di 44 ms);
- [x] banchi globali A/B contro HEAD (fisso identico);
- [ ] ascolto su brano caricato e mixer con un cambio brusco vero;
- [ ] commit (non fatto su richiesta).

### 47. All'avvio del brano parte da ~150 e scende per molti secondi prima di allinearsi 🔴 (2026-09-17, segnalato, non indagato)

Segnalazione durante l'ascolto: brano a ~108 BPM; all'apertura dell'app il
tempo è già circa 150, e quando il brano parte ci mette molti secondi, scendendo,
prima di stabilizzarsi. Atteso: aggancio in pochi quarti. Percorso diverso
dall'item 46 (acquisizione, non cambio a brano agganciato).

- [ ] capire da dove viene ~150 prima del brano (stanza/silenzio agganciato? tempo precedente tenuto da `notifyInputRestart`?);
- [ ] riprodurre offline sul file dell'utente con la rete vera e la traccia `VP_TEMPO_TRACE`;
- [ ] misurare quanto del ritardo è acquisizione del decoder e quanto è la discesa del clock.

### 48. Griglia sbagliata non d'ottava difesa per tutto il brano dopo un buco 🟡 (2026-09-28, banchi ok — resta ascolto)

FEEL da file: il decoder pubblica ~132 per ~46 s mentre il fold legge ~103 a
salienza 1.00. `postHoleReopenSec` in `BeatDecoder` è un timbro che **non scade
mai**: dopo il primo buco da 2,5 quarti con la parte che suona, il rifiuto
`refusePostHoleComb` blocca per il resto del brano ogni correzione non d'ottava
dal fold (salvo 4-beat già sul fold). Il rifiuto esiste per difendere una
griglia giusta prima della pausa (fixture D, 100→150); ora difende solo una
griglia che il fold ha **almeno una volta confermato** (`combAgreedBpm`,
accordo modulo ottave entro `kStaleGridRelease`, stesso livello entro
`kStaleGridThreshold`). Nessuna soglia nuova, nessun titolo.

- [x] banchi, controllo = HEAD ricompilato: `probe_motion_matrix --quick` (anche `--product-direct`), `probe_tempo_step`, `VPAlign --ramps`/`--steps` **byte-identici**; `VPTests --tempo-step` 14/0, `--tempo-slow` 10/0, `--new-input` 15/0, `--bar` 10/0 su entrambi. Fixture post-pausa ricostruita (100 corroborato, buco 3/4/6 quarti, poi reticolo 150 per 30 s): 100.00 fisso in controllo e candidato;
- [x] `VPTrack --player --step 1 --bpm 104` su FEEL: primo aggancio tenuto 3 s **46.09 → 24.88 s**, tempo nel 2% **50.3 → 58.8 %**; a 26 s pubblica 102.6 (prima 132.7). Dopo 60 s differenza media 0.05 BPM (max 1.06), secondi fuori 2% 79 → 78;
- [x] controlli reali: EVERYTIME 44.1k, SPLENDIDA, Sally identici al byte; EVERYTIME 48k differisce solo sull'ultima cifra di due righe (scheduler del worker), riepilogo identico;
- [x] **"secondo brano: si allinea solo dopo STOP" (2026-09-28)** — `VPTrack --then B.wav --at 60 [--stop-after X --stop-gap G]`. Build *prima* di questo item, SPLENDIDA→FEEL: senza STOP aggancio tenuto a **46.08 s** dal cambio, con STOP a +15 s e START a +18 s a **28.97 s** (STOP toglie `sounding` e quindi il rifiuto post-buco). Con questo item, senza STOP: **24.88 s**. EVERYTIME→SPLENDIDA e SPLENDIDA→EVERYTIME: identici con e senza STOP in entrambe le build (2.06 / 5.31 s). Da t≈+10 s le tracce con e senza STOP coincidono anche in fase.
- [ ] ascolto.

### 49. EVERYTIME: il fill fa uscire il tempo e il clock slitta di un battito 🟡 (2026-09-28, parziale: 44.1 kHz risolto, 48 kHz no — resta ascolto)

**Causa, misurata.** Griglia del brano prima del fill (110-136 s) 123.04 BPM,
dopo (166-186 s) 122.88: la prima retta, prolungata di 115 battiti, cade sulla
seconda a -23 ms. **Il brano non accelera.** Il decoder, FISSO da 204 battiti,
esce a 141 s su due voti veloci mentre i due fit sono 2-3 volte peggio piazzati
del solito (fiducia 0.30) e il comb resta su 123; pubblica fino a 129 e il clock
anticipa -206 ms a 150 s e **slitta di un battito intero** verso 156 s (l'1 si
sposta). Misura: `scripts/analysis/line_check.py` (nuovo).

**Tenuto.** `BeatDecoder`: su feed diretto non si esce da un FISSO lungo
(>= 2 finestre lunghe, 48 battiti) su soli voti veloci quando *tutti* dicono
"stesso tempo": fiducia di piazzamento (`EvidenceTrust`, la stessa del tracker)
< 0.50, spostamento del fit corto < `kGridStepMinimum` (2.5%), finestra lunga non
`moving`, comb che non segue il fit corto (confronto modulo ottava, almeno 1/4
della strada). L'evidenza sostenuta (6 battiti sbandati, 5 oltre 4.5%, ancora
6%) esce comunque.

- [x] EVERYTIME 44.1k `VPTrack --player`: tempo nel 2% **94.0 → 98.6%**; errore reale del clock nel fill da -232 ms + slittamento di un battito a **-54 ms max, nessuno slittamento**.
- [x] Banco a griglia nota (`probe_motion_matrix --quick`, entrambe le corsie): offset 0 **identico**; offset 32 fisso 20.9/50.5 → **20.4/47.4** (un falso rilascio in meno); offset 48 gradino 33.7/145.6 → **33.2/140.3**; offset 16 gradino 33.6/153.9 → 34.0/154.2 (solo il seme 257997, già rotto: 21% di errore BPM, livello sbagliato). Matrice completa: gradino 37.6/150.7 → 37.6/150.4, continuo invariato. `probe_tempo_step`, `VPAlign --ramps/--steps` identici; `VPTests --tempo-step/--tempo-slow/--new-input/--bar/--state-timing` identici. FEEL, SPLENDIDA, Sally identici al byte.
- [ ] **EVERYTIME 48k non migliora** (90.6% → 90.6%, slitta ancora a 142-156 s): a 99.8 s la fiducia rimbalza a 1.00 per un battito (fit corto del fill momentaneamente pulito) e lo spostamento è +3.9%, quindi esce; tornato FISSO l'anzianità riparte e a 141.7 s ha 22 battiti. Isteresi sulla fiducia o anzianità "per tempo" sarebbero soglie da un solo brano: servono altri fill reali con griglia (usare `line_check.py` su brani a tempo costante).
- [ ] ascolto su iPad.

Scartati (stessa sessione, controllo = HEAD): sola fiducia (continuo 38.8/95.0 → 41.8/111.1, `VPAlign --ramps` FAIL, 140→75 13.6 → 24 s); + comb che arriva sul fit corto (rampa 12 s FAIL 84.1 → 107.0); + comb solo di direzione e modulo ottava (continuo 39.6/100.0, 140→75 ancora 24 s); + esenzione "curva provata" g >= 0.50 (banco quasi pari ma EVERYTIME torna 94.0%: anche il fill ha g 0.66); stime a 4 battiti ripetute come prova di gradino (il fill corre coerente per 3 battiti: 126.7/127.3/125.1); anzianità senza tetto 2.5% (gradino offset 32: 242175 p95 113 → 190 ms).

### 50. Secondo brano caricato con START acceso: percussioni sempre un po' in anticipo 🟢 (2026-09-28, corretto e misurato — resta ascolto)

Segnalazione: dopo il cambio file le percussioni stanno sempre leggermente
avanti; lo stesso brano caricato per primo dopo un avvio pulito è a tempo.
**Causa:** al cambio file il worker (`NeuralBeatTracker`) azzera estrattore e
resampler e compensava con un riempimento fisso `frame - hop`, che ignora i
campioni rimasti a metà hop e nel resampler: da lì ogni ipotesi era datata
presto (fino a ~20 ms), la fase proiettata troppo avanti, e l'errore **si
sommava** a ogni file caricato. Ora ogni frame è datato dal campione d'ingresso
esatto da cui l'estrattore è ripartito, come al primo avvio.

- [x] `VPTrack --then` (INFINITO → SPLENDIDA a 60 s) contro SPLENDIDA da sola: **+16.07 ms in anticipo → 0.00 ms**; con il cambio spostato di 7/13 ms +15 → -1 ms; FEEL → INFINITO +0.8 ms. Un brano caricato per primo è identico al byte.
- [x] Nuovo test `VPTests --new-input`: "a file loaded second is dated like a file loaded first" — **FAIL col codice vecchio** (640 campioni, 13 ms), PASS col nuovo; 16/0. `--phase-lock` 15/0, `--state-timing`, `--bar` 10/0, `--tempo-step` 14/0 identici al controllo.
- [ ] ascolto su iPad (vale anche per i buchi da sovraccarico del worker: stesso azzeramento).

### 51. Banco reale con griglia ricavata dal brano (2026-09-28)

`scripts/analysis/line_scan.py` trova da solo, in ogni brano, coppie di tratti
stabili la cui retta prolungata cade sull'altro, e misura lì l'errore reale del
clock e gli slittamenti di battito. Su EVERYTIME ritrova lo slittamento del
fill (t=150, 241 ms, 2 salti) e la sua scomparsa con l'item 49 (67 ms, 0).
Banco: 9 brani dell'utente a 44.1 e 48 kHz + 5 estratti Flamingo, in
`VPTrack --player --pulses`. L'item 49 è **neutro** su tutti e 22 (identici o
rumore di scheduling all'ultima cifra).

Uscite trovate (tolleranza 20 ms, da analizzare come EVERYTIME, senza tarare
sul brano):
- [ ] ASPETTANDO IL SOLE ~172 s: 190 ms, a entrambe le frequenze;
- [ ] SPLENDIDA ~105 s (44.1k): 209 ms;
- [ ] LET ME LOVE YOU ~74 s (44.1k): 151 ms, 1 slittamento;
- [ ] VITA ~90 s (44.1k): 124 ms;
- [ ] **FEEL a 48 kHz finisce a 208 BPM** (44.1k: 106): livello doppio a seconda della frequenza. L'iPad lavora a 48 kHz.
- nota: con tolleranza 12 ms quasi nessun brano verifica tratti; il clock oscilla più di 12 ms rms su 16 s, oppure il tempo del brano non è costante (Flamingo live: 0 s, come atteso).

### 52. EVERYTIME a 48 kHz e dipendenza dalla frequenza del dispositivo: due candidati respinti 🔴 (2026-09-28)

Banco: 9 brani + EVERYTIME a 44.1/48 kHz + 5 estratti Flamingo, `line_scan.py`
(tolleranza 20 ms), più banco sintetico completo. Controllo = HEAD (item 49/50).

- **Respinto: regola fill più robusta** (fiducia scarsa "appiccicosa" per
  `kShortFit` battiti, tetto dello spostamento a `kStaleGridThreshold`).
  EVERYTIME 48k slitta ancora (239 → 242 ms, stesso salto); gradino sintetico
  peggiora (offset 32 41.4/168.6 → 42.1/176.5; completa 37.6/150.4 →
  37.9/152.3). Nessun altro brano reale cambia.
- **Respinto: filtro anti-aliasing prima del resampler** (`LinearResampler` non
  filtra: 44.1k è decimazione pura, 48k interpolazione lineare; l'energia sopra
  11 kHz si ripiega diversamente per frequenza). FIR Kaiser -61 dB oltre
  11.025 kHz, ritardo compensato. Il banco peggiora nel complesso: FEEL 44.1k
  106 → **209**, BLUE SKY 48k 85 → **171**, EVERYTIME 44.1k torna a slittare
  (4 salti), VITA 124 → 300 ms, `VPTests --phase-lock` 156 BPM letto **78**
  (FAIL). Migliorano ASPETTANDO (190 → 51 ms) e LET ME LOVE YOU (151 → 77 ms).
  La catena (soglie, bande alte cassa/charleston, ottave) è tarata sull'ingresso
  attuale con l'aliasing: cambiarlo sposta le ottave in modo imprevedibile. Non
  riproporlo senza ritarare e rimisurare tutto.
- Osservazione: 44.1k e 48k danno risultati diversi sugli stessi brani, ma
  nessuna delle due è sistematicamente migliore (FEEL e EVERYTIME meglio a
  44.1k; SPLENDIDA, VITA, LET ME LOVE YOU meglio a 48k). L'iPad lavora a 48k.
- [ ] EVERYTIME 48k: slittamento a ~150 s ancora aperto.

### 53. Suono delle percussioni: conga dal loop scelto, volume, DANCE di default 🟡 (2026-09-28 — resta ascolto)

Richiesta: suoni realistici, quelli di SampleFocus "Salsa Congas Loop - Dance 3"
tranne la conga più bassa (le note basse sulla conga media). Poi: conga troppo
basse rispetto agli altri suoni (knob sempre al massimo) e la conga alta
stoppata molto più bassa dell'altra. Figura di default: DANCE.

- [x] `scripts/prepare_loop_congas.py` (numpy) taglia il loop in `Assets/Percussion`: aperti, slap, slap chiuso, heel, toe, muff, tapado veri; code degli aperti modellate fino a 300 ms; attacco allineato (picco a 2-3 ms). Licenza: non CC0, vedi ATTRIBUTION.md.
- [x] `PercussionEngine`: intonazione naturale (`kDrumTune` 1.0), colpi stoppati dal loro file senza smorzamento artificiale, conga +5 dB (`kCongaLevel`), soft clip sopra 0.80.
- [x] Volume percepito (dBA, 150 ms, vel 0.9): conga da ~-24 a ~-19 (battimani -18.5, cembalo -21); slap chiuso a velocità media da 6.2 a 1.3 dB sotto l'aperto.
- [x] Default DANCE + migrazione una tantum delle preferenze; test del default aggiornato.
- [x] `VPTests --percussion` 17/0; allineamento attacchi identico a HEAD (il FAIL da 2.7 ms è il battimani sintetico, preesistente); `--leak` righe MIXER identiche. Percorso microfono iPad peggiorato (righe no-leak e seam): accettato, per ora solo mixer e brani caricati.
- [x] Shaker sostituito con `soft-bright-shaker_128bpm.wav` (`scripts/prepare_loop_shaker.py`): accento = `shaker_down`, i tre colpi leggeri diversi = `shaker_up`/`_b`/`_med` e `shaker_down_med`; coda naturale, senza il decadimento imposto dei vecchi VCSL. Volume -25.4 dBA come prima (conga -19). Allineamento attacchi: dispersione 3.04 ms (HEAD 2.85, prima dei suoni 2.71; soglia 2 ms già rossa per il battimani sintetico): il resto è lo shaker su ±2.5 ms, dove il soft clip arrotonda la cima della sua salita lenta a velocità alta. `--percussion` 17/0, righe MIXER di `--leak` invariate.
- [ ] ascolto su iPad.

### 54. Brano caricato: a metà brano il tempo crolla (99 -> 60, 96 -> 62) e poi riprende 🟢 (2026-09-28, corretto e misurato — resta ascolto)

Segnalazione dall'iPad, brano caricato: LET ME LOVE YOU a metà da 99 a 60, VITA
a 62, poi ripresi. **Causa:** dopo un passaggio quasi silenzioso la ripresa
della band fa scattare l'epoca "a freddo" di `updateAnalysisEpoch` (pensata per
stanza vuota -> band), che azzera comb e modello mentre la parte suona; il
nuovo aggancio passa ~10 s su livelli sbagliati. Riprodotto sul Mac: LET ME LOVE
YOU a 112 s (48 kHz: clock fino a 78), VITA a 79-83 s, BLUE SKY 48 kHz a 67 s
(clock fino a 178).

**Correzione** (`VirtualPercussionEngine`): per un brano caricato, dopo che la
parte ha suonato su un livello confermato, quell'epoca diventa un ingresso di
arrangiamento (comb e modello conservati, si butta solo la griglia). Il fatto
"ha suonato su un livello confermato" è memorizzato: `levelSettled` letto
nell'istante valeva 0 proprio nel blocco del riavvio a 48 kHz. Il mixer
mantiene l'epoca a freddo (lì una pausa e una band possono essere il brano dopo).

- [x] Clock nei 15 s dopo il riavvio: LET ME LOVE YOU 48k 78-98 -> **91-100**, 44.1k 99-115 -> 100-104; VITA 44.1k 94-98 -> 94.5-97, errore peggiore contro la griglia vera 124 -> **72 ms**; BLUE SKY 48k 85-178 -> **86-88**. Tutti gli altri 16 file invariati (rumore di scheduling). `VPTests --new-input` 16/0, `--rhythm` 3/0, `--bar` 10/0, `--makeup b/c/e/f` verdi; `--makeup a` 53/7 e `d` 10/2 identici a prima dei cambi di suono (preesistenti).
- [x] Scartata prima: tenere il tempo nel tracker durante il riaggancio. Si rilasciava sulle ultime ipotesi del decoder vecchio, poi sulla prima ipotesi nuova vicina per caso; VITA 44.1k faceva un picco del clock a 111.
- Nota: con il comb conservato LET ME LOVE YOU 44.1k passa da 197 a 99.5 al riavvio (l'ottava del comb). Sull'iPad era già a 99.
- [ ] ascolto su iPad.

Suite completa prima/dopo i cambi di suono (item 53): 652/31 -> 647/36. Nuovi FAIL solo sul percorso altoparlante/microfono iPad (3 righe del cancellatore e la battuta dall'armonia senza batteria, 100% -> 4%: le conga a 325 Hz passano l'altoparlante simulato e finiscono nel cromagramma). Il test del canale cassa fallisce uguale prima e dopo.
`VPTrack` ha ora anche `--seek-at T --seek-to U` (seek nella forma d'onda).

### 55. Traccia lunga con più brani: dopo un salto nella forma d'onda il nuovo BPM arriva tardi 🟢 (2026-09-28, corretto e misurato — resta ascolto)

Segnalazione: in una traccia con tanti brani, saltando a un brano con un BPM
molto diverso il tempo nuovo arriva dopo molto; con uno STOP arriva subito.
Causa: `notifyTrackSeek` apriva solo la finestra dell'1. Il decoder teneva
griglia, fit, comb e modello del brano di prima e, con la parte che suona, le
guardie che difendono la griglia (keep sull'ultimo battito, rifiuto dopo un
buco, livello tenuto) la difendevano contro il brano nuovo. Ora un seek è un
nuovo ingresso come un file nuovo: `notifyTrackSeek` chiama
`notifyInputRestart` (epoca a freddo, coda scartata, parte in silenzio fino
alla nuova griglia, clock azzerato come al cambio file). L'inseguimento dentro
un brano non passa di qui: matrice, VPAlign e rampe sono identici per
costruzione.

File di prova in `/tmp/vp-multisong` (EVERYTIME 123 / BLUE SKY 87 / SPLENDIDA
108, `mk.py`), metrica `score.py`: primo tratto di 3 s entro il 2% (ottava
ammessa) dopo il salto.

- [x] Salto fra brani diversi: EVERYTIME -> BLUE SKY **9.5 -> 1.5 s**, BLUE SKY -> EVERYTIME **13.5 -> 3.0 s**.
- [x] Salto dentro lo stesso brano: SPLENDIDA 9.0 -> **1.5 s**, EVERYTIME 0 -> 3.0 s, BLUE SKY **0 -> 10 s** (l'acquisizione a freddo del corpo di BLUE SKY legge 135 per ~8 s). Costo accettato: un salto è un riaggancio.
- [x] Scartate: (a) epoca conservata (comb e modello tenuti, parte che suona): 9.5 / 10.5 s, il comb vecchio vota per ~5 s; (b) epoca a freddo senza silenziare la parte: 3.5 / 15.5 s; (c) comb conservato con la parte silenziata: BLUE SKY -> EVERYTIME mai.
- [x] `VPTests --bar` 10/0 (il test del seek ora chiede un'epoca nuova e l'1 giusto a metà quarto; prima chiedeva il contrario), `--new-input` e `--state-timing` verdi.
- [ ] ascolto su iPad.

Aperto, non toccato: il **cambio brano naturale** dentro il file (senza salto).
Con 1.5 s di silenzio fra i brani: BLUE SKY -> EVERYTIME 32.5 s, SPLENDIDA ->
EVERYTIME 16 s, EVERYTIME -> BLUE SKY 14 s. Una pausa e poi una band nel
file è esattamente il caso che l'item 54 tratta come stesso brano. Nel probe
uno STOP di 1 s a 8.5 s dal cambio non aggancia subito (16 -> 11, 14 -> 10.5,
32.5 -> 45 s), e neppure un riavvio a freddo: EVERYTIME a ~28 s legge 107 per
~20 s (stessa acquisizione sbagliata di FEEL). Un avviso "premi STOP"
prometterebbe un rimedio che il probe non mostra.

### 56. iPad: al primo ridimensionamento/spostamento della finestra AirPods muti 🟡 (2026-09-29, nuova correzione da ascoltare)

Sintomo: solo la prima volta dopo l'avvio (e dopo ogni rebuild), ridimensionare
o spostare la finestra fa un crack e poi niente audio; cambiare CLOCK o BUFFER
lo fa tornare, e dopo non succede più. Uscita AirPods, ingresso microfono iPad.

Prima ipotesi (solo lettura del codice): la prima apertura passa rate 0,
`chooseBestSampleRate` la trasforma nei 44100 del device appena costruito e il
Pimpl la teneva come `targetSampleRate`, poi `restart()` la richiedeva a ogni
cambio di route. Il log la conferma a metà (apertura 44100 → concessi 48000), e
`open()` ora fissa `targetSampleRate = sampleRate`; ma **non era il silenzio**.

Misura (build con `JUCE_IOS_AUDIO_LOGGING=1` + riga `VPDIAG` al secondo in
`MainComponent::timerCallback`, avviata da Xcode): al ridimensionamento nessun
cambio di route e nessun `restart()`; callback regolari (~195/s), l'app scrive
il brano (picco in uscita 0.4), solo **xrun 20 → 26**. Lanciata da `devicectl`
(senza debugger) gli xrun restano a 1 e il difetto non compare.
Un possibile guasto era che `process()` restituiva a RemoteIO l'errore di `AudioUnitRender`
dell'**ingresso**; dopo una raffica di xrun (microfono iPad e AirPods hanno due
clock) quella lettura può restare in errore, e un render callback che ritorna
errore fa suonare silenzio all'uscita finché il device non viene riaperto.
Fix in `juce_Audio_ios.cpp` (`process`): se la lettura del microfono fallisce,
quel ciclo ha ingresso muto e ritorna `noErr`. Contatore
`inputRenderFailures` (per ora sommato ×1000 a `getXRunCount`, solo diagnosi).

Conferma dell'utente (2026-09-29): il problema persiste in **BRANO**. Nel log
con AirPods muti: 192–197 callback/s, `play=1`, `ready=1`, `outpk` fino a 0.5,
`rebuild=0`, `xrun=1`. Il buffer RemoteIO era già 4096 frame: l'ipotesi di una
prima allocazione nel callback è stata provata e scartata. L'errore della
lettura microfono non compare in questo log (`xrun` non supera 1000); il file
produce audio, ma RemoteIO non lo consegna agli AirPods. BRANO ora apre
Playback con zero ingressi; MIXER conserva PlayAndRecord. Resta la prova sul
primo resize della nuova build.

Nuova prova (2026-09-29): BRANO apre davvero con `inputChannelsWanted: 0`.
Al primo allargamento il suono fa crack, poi torna distorto; nessun cambio di
route, callback ~195/s e `outpk` 0.2–0.45, ma gli xrun salgono da 1 a 7.
L'uscita senza microfono ha tolto il silenzio permanente, non il guasto
dell'unità dopo lo xrun. Ora un resize di BRANO su A2DP che aggiunge xrun fa
riaprire **una volta** la stessa unità alla stessa frequenza, dopo 0.4 s senza
altri cambi di dimensione. Non richiama `engine.prepare()`. Da verificare sul
device: che la distorsione sparisca dopo il recupero e che la riapertura non
introduca una pausa peggiore.

- [x] Log sul device: callback e uscita software attivi anche durante il silenzio.
- [ ] Ascolto della build con recupero: BRANO in play, primo ridimensionamento;
      `VPDIAG resize xruns` e `riavvii 1 AirPods resize xrun` devono coincidere
      con il gesto. Verificare anche BRANO ↔ MIXER per il ritorno dell'ingresso.
- [ ] Dopo la conferma: togliere `VPDIAG` (MainComponent, JUCE log a 0, ×1000 in `getXRunCount`).
- Nota: gli xrun sotto debugger possono contribuire, ma il nuovo log mostra
  silenzio con `xrun=1`: non sono condizione necessaria.

### 57. Ottava sbagliata a brano avviato (THE REASON 85 → 170; LET ME LOVE YOU, FEEL 48k, UNA CANZONE sul doppio) 🟢 (2026-09-28, corretto e misurato — resta ascolto)

Due strade, una regola: l'ottava cambia solo a mano, con un file nuovo o un
salto, mai da sola sotto la parte che suona — tranne quando è l'analisi stessa
a cambiare tempo in quel momento.
- THE REASON: un ingresso di arrangiamento a 167.7 s butta la griglia e il nuovo
  aggancio sceglie 170; il clock la prendeva in un blocco. `BeatTracker::
  holdSoundingLevel`: con la parte che suona un'ipotesi al doppio/metà esatti
  (±7%) del tempo suonato non va al clock e il livello viene riportato.
- LET ME LOVE YOU (entra a 165 = 5:3 di 99, poi salta a 190), FEEL 48k (158 =
  3:2 di 104 → 205), UNA CANZONE PER TE (114 → 180): la parte entra su una
  lettura provvisoria sbagliata e poi l'analisi salta da sola sul doppio, che
  resta per tutto il brano. Nello stesso metodo: se l'analisi salta di oltre
  ~11% (non un'ottava) mentre la parte suona, in AUTO il nuovo tempo viene messo
  nella fascia 49–168 come prima dell'entrata.

- [x] Banco (item 58), solo questa regola contro la base: LET ME LOVE YOU 198 → **99** (44.1 e 48k), UNA CANZONE 172 → **86** (entrambe), FEEL 48k 209 → **104**; altri 17 casi identici (INFINITO 48k fase 42.7 → 41.2 ms); slittamenti 1 → 0. THE REASON: 0 campioni sopra 110 BPM a 44.1 e 48k (prima 128).
- [x] `VPTests --tempo-step` 14/0, `--tempo-slow` 10/0, `--state-timing` 15/0, `--new-input` 16/0, `--bar` 10/0; `--octave` 2/9 **identico** a HEAD (rosso preesistente).
- [ ] Ascolto su iPad (THE REASON, LET ME LOVE YOU, FEEL, UNA CANZONE).

### 58. Banco globale sui brani veri e correzione di fase adattiva 🟢 (2026-09-28, misurato — resta ascolto)

`scripts/analysis/bench_songs.py run TAG mp3...` suona 11 brani dell'utente a
44.1 e 48 kHz nell'app intera (`VPTrack --player --pulses`, cache in
`/tmp/vp-bench`, elenco in `/tmp/vp-bench/songs.txt`); `show TAG [TAG2]` confronta.
Misure: **scatti** (rms % della velocità sentita contro la sua mediana su 8 s:
il "corre/trascina"), **fuori** (% oltre il 3% dal tempogramma; secondo parere
rumoroso), secondi all'ottava, fase `line_scan.py`. Regola: una modifica passa se
migliora l'insieme e non peggiora nessun brano.

Correzione di fase (`TempoFollower`, HIGH): con ingresso diretto il clock
chiudeva la fase piegando la velocità fino al 7.5% anche su un tempo fermo.
- Scartata A (binario diretto al 3%): scatti 0.88 → 0.79, fase peggiore 49 → 44 ms.
- Scartata B (A + tetto ordinario 25% → 10%): scatti **0.88 → 0.75**, ma matrice
  a fase nota con ingresso diretto: continuo 40.2/115.0 → **46.9/123.3** ms,
  gradino 34.9/145.3 → 36.1/157.8. Segue peggio le inflessioni vere.
- **Tenuta C**: 3% solo se nulla dice che la band si muove (glide veloce, piega
  tenuta, curva provata, transizione, suggerimento di moto) **e** l'errore è
  entro `kOpenAbove` (0.06 battito); altrimenti il 7.5% pieno. Matrice: corsia
  normale identica; ingresso diretto fisso 22.4/72.1 → 22.1/72.8, continuo
  40.2/115.0/279.9 → 40.6/113.7/**237.2**, gradino 34.9/145.3 → 34.6/145.7.
  `probe_recovery` 0 FAIL (20 ms diretto 0.437 s invariato; 50 ms: piega 6.24 →
  3.60 BPM). `VPAlign --ramps/--steps` identici a HEAD byte per byte.
  Banco (contro la sola regola d'ottava): scatti 0.97 → **0.93**; THE REASON 44k
  0.96 → 0.81, ASPETTANDO 1.29 → 1.18 e 0.88 → 0.71, UNA CANZONE 1.22 → 1.08,
  BLUE SKY 1.27 → 1.14; +0.01 su tre brani (rumore).
- [ ] Ascolto su iPad.
- Osservazione: il banco segna **fuori ~25%** su quasi tutti i brani: il
  tempogramma a 12 s è troppo rumoroso per fare da verità; serve una griglia
  migliore (strada 5 del piano: annotazioni offline).

### 59. Aggancio iniziale: il pettine giusto non riusciva a correggere una griglia 3:2 🟡 (2026-09-29, un meccanismo corretto — resta il resto dell'aggancio)

Banco (item 58) con due misure nuove: **aggancio** (secondi dall'inizio del file
al primo tratto di 8 s entro il 3% del tempo del brano) e **sbagl s** (secondi
con la parte che suona su un tempo sbagliato). Il tempo di riferimento è la
mediana del clock a 60-100 s, solo nello script: l'app non lo vede. Base (dopo
item 57/58): aggancio medio 14.4 s; peggiori ASPETTANDO 46/28 s, LET ME LOVE
YOU 28/24 s, FEEL 27/19 s. Nota: su brani che accelerano (LET ME LOVE YOU 96 →
100) la misura penalizza un aggancio già giusto; serve una griglia di
riferimento migliore (item 58, strada 5).

ASPETTANDO 44.1k: pubblicato 131-133 (3:2) per 46 s, pettine a 88 con livello
assestato già da 14 s. Due ostacoli, entrambi nel voto d'ottava del decoder:
- la lettura grezza del pettine oscillava fra due ottave dello **stesso** battito
  (87 ↔ 176): ogni cambio ricominciava il voto. Ora, se il disaccordo con la
  griglia non è un'ottava, i voti per qualunque ottava di quel battito sono lo
  stesso voto; una discussione d'ottava vota ancora per livello;
- il rifiuto "dopo un buco" (fixture D) difendeva 132 perché il pettine l'aveva
  "confermato" a 6-8 s, prima di assestarsi. Ora conferma una griglia solo un
  pettine con livello assestato.
- In più il veto `unprovenSlowerOctave` (pettine più lento su griglia sana e
  fitta) ha una via d'uscita per letture non d'ottava: 16 battiti di pettine
  fermo sullo stesso livello, assestato e saliente, senza transizioni.

- [x] Banco: ASPETTANDO 44.1k aggancio **46.4 → 25.4 s**, sbagliato 37.9 → 15.8 s; gli altri 21 casi identici. Esecuzioni deterministiche (stessi numeri da sole o in parallelo).
- [x] `probe_matrix --quick` identico (7.15 s, 19 uscite, 6 mai agganciati); `probe_motion_matrix --quick` identico in entrambe le corsie; fixture D ricostruita (100 → buco 1.8/2.4/3.6/4 s → reticolo 150): suonando resta **100**, senza parte **150**, identico a prima; `VPTests --tempo-step` 14/0, `--tempo-slow` 10/0, `--state-timing` 15/0, `--new-input` 16/0, `--bar` 10/0, `--octave` identico a HEAD; `VPAlign --ramps/--steps` identici.
- [ ] Resto dell'aggancio (FEEL, UNA CANZONE, THE REASON, EVERYTIME ~12-27 s): la parte entra su una lettura provvisoria prima che il pettine si assesti (~10-16 s). Decisione di prodotto aperta: entrare subito rischiando il tempo sbagliato, o aspettare la conferma del pettine.

**Respinta (2026-09-29): entrata della parte confermata dal pettine ("via di
mezzo").** Entrare sul primo quarto utile solo se griglia e pettine concordano
(entro il 4%), altrimenti aspettare al massimo 8 s. Banco con due colonne nuove
(`entra`, `sb<40` = secondi sul tempo sbagliato nei primi 40 s):
- accordo a meno di un'ottava: sb<40 totale **119 → 158**, sbagliato su tutto
  il brano 472 → 884; LET ME LOVE YOU 48k tutto a **198**, EVERYTIME 44.1k
  tutto a **62**. Migliorano FEEL 44.1k (18.9 → 4.1), ASPETTANDO, EVERYTIME 48k.
- accordo sullo stesso livello e dentro 49-168: sb<40 **119 → 131**, totale
  472 → 664; EVERYTIME 44.1k ancora a 62 per tutto il brano, THE REASON 44.1k
  2.8 → 15.0, UNA CANZONE peggio; migliorano LET ME LOVE YOU 48k (8.7 → 1.4) e
  FEEL.
Motivo: spostare l'istante di entrata cambia quale ottava resta tenuta dalla
regola che tiene il livello sotto la parte (item 57). Su EVERYTIME griglia e
pettine concordavano sulla metà, dentro la fascia normale. Tolta; non
riproporla senza separare la scelta dell'ottava dall'istante di entrata.

**Correzione dell'item 57 (2026-09-29): ÷2/×2 non cambiavano il tempo.** La
regola che tiene l'ottava sotto la parte che suona riportava indietro anche la
pressione manuale (EVERYTIME ×2: 194 → 123 in 4 s), e in modalità manuale
modificava `userOctave`, che il blocco dopo veniva reimpostato da
`cfg.tempoOctave`: le due cose si rincorrevano. Ora la regola agisce solo in
AUTO; un livello manuale è dell'ascoltatore. `VPTrack --octave-at T --octave N`
simula la pressione: EVERYTIME ÷2 128 → 61, VITA ×2 96 → 192, stabili. ÷2 resta
senza effetto se la metà scende sotto 50 BPM (`kMinBpm`, voluto: VITA 96 → 48).

### 60. Verifica dell'item 59 e aggancio iniziale: una regressione chiusa, un caso migliorato 🟢 (2026-09-29, misurato — resta ascolto su iPad)

Controllo dopo il commit 21861d7 (item 59) su brano caricato e mixer.

**Regressione trovata e corretta.** `probe_tempo_step` era FAIL su HEAD: il salto
120 → 160 finiva a **53.3 BPM**. Bisect: PASS fino a 21861d7~1, FAIL da 21861d7;
isolata la terza modifica dell'item 59 (via d'uscita dal veto `unprovenSlowerOctave`
dopo 16 battiti di pettine fermo). Dopo il salto il pettine vecchio si assesta su
**un terzo** della nuova griglia, fermo per 16 battiti appena finita la quarantena,
e la prova gli consegnava il tempo. Il commento accanto (`combCleanHalf`) diceva già
che un sottomultiplo non deve mai guadagnarla. Ora `combOtherSlower` esclude i
sottomultipli interi (rapporto griglia/pettine vicino a 3, 4, …, entro `kSteadyFold`);
le griglie 3:2 e 5:3 per cui l'item 59 era nato (132/88, 165/99) restano ammesse.
- [x] `probe_tempo_step` di nuovo **PASS**, tabella identica a prima dell'item 59.
- [x] ASPETTANDO (il caso dell'item 59) identico sul banco brani: il guadagno resta.

**LET ME LOVE YOU 44.1k: 11 s su 153 sopra una canzone a 98.** Traccia del voto
d'ottava (stampa temporanea, rimossa): la griglia 165 perdeva `provisional` su un
pettine di passaggio (144, salienza 0.22, verso 196); poi il pettine giusto
(196 = 2×98, salienza 1.00) doveva raccogliere 4 voti, ma mentre la parte suona la
griglia sbagliata accetta ~1 colpo su 4 (ogni colpo accettato chiude un buco di
>2.5 battiti) e il flag `levelSettled` lampeggia da un refresh all'altro: ogni
battito "non assestato" toglieva un voto. Corretto in `nonOctaveDisagreement`: una
griglia che il pettine **non ha mai confermato** da quando è iniziato l'ingresso
(`combAgreedBpm`), **che sta morendo di fame** (il colpo accettato chiude un buco di
più di `kGridStaleBeats` periodi) e **con la parte che suona** è ancora un'ipotesi
d'aggancio: il disaccordo non d'ottava vale anche senza `levelSettled`. Ogni
condizione è misurata:
- senza "mai confermata" (anche "non confermata a questo tempo"): dopo un vero
  salto il pettine vecchio votava → gradino offset 16 34.05/154.2 → 34.45/157.5;
- senza "affamata": swing 0.61 a 132 (seed 321349) scattava sull'88 del pettine
  (cella lunga-corta) → stessa perdita sul gradino;
- senza "suona": `VPAlign --ramps` rampa 12 s MIXER 35.5 → 37.2 ms (da muta la
  griglia affamata si riaggancia da sola col waive del gate sul battito).
- [x] Banco brani (22 casi): LET ME LOVE YOU 44k aggancio **27.9 → 20.3 s**, sbagliato
  nei primi 40 s **19.5 → 10.4 s**, sbagliato totale 29.0 → 20.0 s; gli altri 21
  identici (I WANNA DANCE 44k da solo è identico byte per byte: le ±0.2 s viste nel
  banco erano non-determinismo con 16 processi in parallelo — il banco va lanciato
  senza compilazioni o test in contemporanea).
- [x] Identici byte per byte a HEAD: `probe_matrix --quick`, `probe_motion_matrix
  --quick --verbose` offset 0/16/32/48 in entrambe le corsie (per seed),
  `VPAlign --ramps` e `--steps`.
- [x] `VPTests --tempo-step` 14/0, `--tempo-slow` 10/0, `--state-timing` 15/0,
  `--new-input` 16/0, `--bar` 10/0.
- Nota: `VPTests --bar` sotto CPU carica fallisce a volte «seek re-aligns the one»
  (beat=1 atteso 2) **anche su HEAD** (2/25 HEAD, 1/25 candidato alternati sotto lo
  stesso carico); da solo passa sempre. Preesistente, non legato a questo item.
- [ ] Ascolto su iPad (LET ME LOVE YOU; e un cambio di tempo netto sul mixer).

### 61. Primo quarto: un conteggio "fidato" sbagliato non si correggeva più 🟢 (2026-09-29, misurato — resta ascolto)

Misura sul banco brani (stampa temporanea dei voti in `BeatTracker`, rimossa;
script `barscan.py` / `barvotes.py` / `barrule.py` / `barok.py` nello scratchpad
della sessione, non nel repo). Riferimento per l'1: dove rete **e** armonia (due
fonti indipendenti) concordano a fine brano; 15 dei 22 casi ne hanno uno.
- La parte entra al quarto successivo (~2 s), prima che i voti di downbeat
  arrivino alla soglia d'ingresso; dopo vale la soglia "mentre suona" (47 battiti)
  e, appena il conteggio è fidato (`barTrustEstablished`, basta che 8 battiti di
  voti diano ragione al conteggio corrente), **solo** spostamenti di mezza battuta.
- Anticipare la decisione non serve: la regola d'ingresso applicata subito dopo
  l'entrata sarebbe giusta 4 volte e sbagliata 6. I primi voti sono inaffidabili.
- Il danno vero: in 5 casi su 22 (FEEL 44k, INFINITO 44k, SPLENDIDA 48k, UNA
  CANZONE 44k/48k) a fine brano rete e armonia concordano che l'1 è **un quarto**
  più in là, e il divieto lo teneva lì per tutto il brano.
- Corretto in `BeatTracker::tryAlignFrom`: sul conteggio fidato uno spostamento di
  un quarto è ammesso se l'altra fonte nomina lo stesso quarto, ognuna col proprio
  margine ordinario (rete: margine "mentre suona" 0.20 su ≥32 battiti; armonia:
  margine base 0.10 su ≥8 cambi, materiale tonale). Un fill o una pausa spostano
  una fonte, non due.
- [x] Secondi suonati fuori dall'1 sui 15 casi con riferimento: **1370 → 914 s**
  (FEEL 44k 252 → 74, INFINITO 44k 271 → 22, SPLENDIDA 48k 149 → 121); nessun
  brano peggiora; negli altri 7 casi nessuna rotazione in più. UNA CANZONE resta
  (armonia troppo debole, margine 0.07: non allentato).
- [x] `VPBar` (materiale sintetico con l'1 vero) identico a HEAD; `VPTests --bar`
  10/0, `--state-timing` 15/0, `--new-input` 16/0, `--tempo-step` 14/0,
  `--tempo-slow` 10/0; `VPAlign --ramps/--steps` identici; clock e tempo invariati
  (cambia solo il numero della battuta).
- **Respinto:** il veto opposto (armonia chiara che blocca lo spostamento di mezza
  battuta della rete). Aiutava SPLENDIDA ma LET ME LOVE YOU 44k 70 → 152 s fuori
  dall'1: a metà brano l'armonia può essere netta e sbagliata.
- [ ] Ascolto su iPad (FEEL, INFINITO, SPLENDIDA GIORNATA).

### 62. `07 1000 GIORNI`: perché fatica (2026-09-29, diagnosi — nessuna modifica specifica)

- **Inizio sbagliato per ~12 s.** L'intro apre con colpi irregolari (0.4–1.9 s);
  l'aggancio veloce li prende come tempo (147.6 a 1 s) e la parte entra a 2.5 s
  su 145–150. Dagli onset la griglia vera è netta da ~3 s (forti ogni 0.372 s =
  161 BPM). Il pettine legge 161–162 da 8 s ma si assesta a 9.5 s, e lo scarto
  (7–10%) cade fra la spinta del pettine (3%) e il controllo griglia vecchia
  (8.7%): corretto solo a 12–15 s. Stessa classe dell'item 59 (entrare subito su
  una lettura provvisoria); una correzione va provata sul banco, non su questo brano.
- **Ottava.** Tempogramma indipendente: alterna 81–86 e 162–172 (ambiguo). L'app
  lo suona a 161–172, dentro la fascia 49–168. Se si conta a ~84, ÷2: a 20 s porta
  la parte a 83–88 e la segue con gli stessi scatti del livello normale (0.93%
  contro 0.78%). Nota di metodo: la metrica "scatti" di `bench_songs.py` conta il
  salto della pressione di ÷2 come oscillazione (EVERYTIME 8.56% invece di 0.84%);
  misurare dopo 10 s dalla pressione.
- La band cambia davvero tempo (≈81 all'inizio, 85–86 al centro, 83 verso la fine):
  l'app segue, 0 slittamenti, scatti 0.79% (media banco 0.94%).
- Battuta: rete e armonia non concordano sull'1 a fine brano (1 contro 0);
  l'item 61 non cambia nulla qui.
- [x] Dall'utente: lo suona a ÷2 (~80); il tempo "a volte sfasa parecchio".
- **Misura della fase a ÷2** (solo analisi offline come righello; nell'app niente
  lookahead, richiesta esplicita dell'utente). Con ÷2 il decoder lavora tutto al
  livello dimezzato (`setUserOctave`): griglia, fit e filtro sui colpi a 80, così i
  battiti della rete a 161 che cadono a metà vengono scartati come suddivisioni e la
  griglia si regge su metà delle prove. Stesso codice, stesso brano: i battiti
  suonati a ÷2 si scostano dalla griglia dell'app a livello normale fino a 130 ms
  per tratti di 10-20 s (1000 GIORNI 20-40, 130-145, 165-175, 215-240 s). Sul banco,
  dove il ÷2 agisce davvero: oltre 60 ms il 17-37% dei battiti (EVERYTIME,
  SPLENDIDA, I WANNA DANCE). Sotto i 100 BPM il ÷2 è ignorato (scenderebbe sotto
  `kMinBpm`).
- Proposta (non fatta, da confermare): decodificare sempre al livello naturale e
  applicare ÷2/×2 solo all'uscita (tempo e fase pubblicati, parità del battito
  fissata alla pressione). Cambiamento profondo: tocca anche l'ottava AUTO.
- Riferimenti tentati e scartati come verità: DP sugli onset (si aggancia al
  contrattempo in 20-50 s, dove il pettine dà ragione all'app), cassa sotto 150 Hz
  (non regolare su questo brano, coerenza 0.02-0.29).

### 63. ÷2 manuale: pubblicato, non decodificato 🟢 (2026-09-29, misurato — resta ascolto su iPad)

Causa (item 62): con ÷2 il decoder lavorava tutto al livello dimezzato, con la
griglia che scarta come suddivisione ogni secondo battito e metà delle prove.
Sul brano stesso, a ÷2, i battiti suonati si scostavano fino a 130 ms da quelli
del livello naturale per tratti di 10-20 s.

Ora (`BeatDecoder::setUserOctave (n, manual)`, `NeuralBeatTracker`,
`BeatTracker`): una richiesta MANUALE più lenta (÷2, ÷4) lascia il decoder al
livello naturale e **pubblica** diviso: tempo, periodo e fase (`(m + fase)/D`),
gli eventi di battito solo per la classe scelta. Tutto causale: la classe è un
conteggio di battiti naturali già passati, niente lookahead.
- La classe segue i battiti che la rete accetta (in 1000 GIORNI passano solo 1 su
  2: una classe presa a caso non vedeva mai un evento); scelta alla pressione
  dall'ultimo battito accettato, ridecisa dal primo battito accettato dopo una
  ricostruzione della griglia; cambia solo se l'altra classe raccoglie molto di
  più (peso > 4x + 5) e allora con un nuovo `gridSerial`.
- Attivo solo se il tempo diviso resta segnalabile: alla pressione da 50 BPM,
  poi da 53, rilascio sotto 49 (isteresi). Sotto, ÷2 è ignorato come prima.
- ×2 e ogni livello scelto da AUTO restano decodificati al livello spostato,
  esattamente come prima. Il primo tentativo pubblicava anche AUTO: UNA CANZONE
  cambiava percorso a 10 s (sbagl s 43 → 69). Percorso naturale/AUTO ora
  byte per byte uguale a HEAD (UNA, FEEL 48k, LET ME LOVE YOU 48k, 1000 GIORNI).
- [x] Banco (22 brani + 1000 GIORNI, ÷2 premuto a 5 s), scatti % contro il vecchio
  ÷2: media 1.08 → **0.76** (SPLENDIDA 2.17 → 0.79, I WANNA DANCE 1.43 → 0.57);
  EVERYTIME 0.62/0.84 → 0.80/0.96 (leggermente peggio). Battiti a ÷2 contro il
  clock naturale dello stesso binario: mediana 0.2 ms, oltre 120 ms il 2.7%
  (prima, sui casi dove ÷2 agisce, 8.9-17%). Contro i battiti accettati (1000
  GIORNI 44k, 60-210 s): oltre 100 ms 3.3% contro 9.5%, MAD 27 contro 34 ms.
- Peggio o aperto: nei primi 30-60 s dopo la pressione il ÷2 nuovo resta 50-100 ms
  fuori dai battiti accettati (transitorio del follower dopo il cambio di
  numeratore); UNA CANZONE (pressione durante l'aggancio, il tempo naturale
  scende sotto 98 e il ÷2 si disattiva: 41% oltre 60 ms).
- [x] Gate: `probe_matrix`, `probe_motion_matrix` (offset 0/32) e
  `probe_tempo_step` identici al riferimento; `VPAlign --ramps/--steps`, `VPBar`
  identici; `VPTests --tempo-step` 14/0, `--tempo-slow` 13/0 (3 nuovi controlli:
  metà tempo e doppio periodo, ogni altro battito, fase senza salti),
  `--state-timing` 15/0, `--new-input` 16/0, `--bar` 10/0; `--octave` 4/7 come HEAD
  (rosso preesistente, dipende dal timing).
- [ ] Ascolto su iPad (1000 GIORNI a ÷2; poi ÷2 → normale e ritorno).
- Metodo: righello offline (script nello scratchpad, non nel repo); nell'app
  nessun lookahead.

### 64. Controllo di non regressione: `--phase-lock` 156 BPM letto 78 dall'item 57 🟡 (2026-09-29, test adeguato alla regola — margine stretto)

Stato del tree a `79d2de4` + test item 63. Build `VPTests` e app macOS pulite.
- [x] Verdi: `--tempo-slow` 13/0, `--tempo-step` 14/0, `--state-timing`,
  `--new-input` 16/0, `--bar` 10/0, `--tempo-motion` 293/0, `--evidence` 2/0,
  `--loops` 58/0, `--percussion` 17/0, `--rhythm` 3/0, `--swing` 3/0,
  `--pop-dance` 3/0, `--transport` 4/0.
- [x] Rossi preesistenti, stessi FAIL ad `aac5a5b` (28/09): `--leak` 46/3
  (righe microfono iPad, accettate nell'item 53), `--harmonic-audio` 8 FAIL.
- [ ] Non girati (oltre 90 s): `--octave`, `--level`, suite completa.
- [x] **`--phase-lock` 15/0 → 14/1**, bisect: verde ad `a25117c`, `6a87604`,
  `29c381c`, `aac5a5b`; rosso da `4389cca` (item 57, `holdSoundingLevel`).
  Trace (`VP_TRACE=1`): la parte entra a 2 s sulla lettura provvisoria 78, a ~5 s
  l'analisi passa da sola a 156; prima il clock la seguiva (×2 automatico sotto
  la parte), ora il livello resta a 78 per tutto il click. È la regola
  dell'item 57 (ottava solo a mano) che fa il suo lavoro, lo stesso caso di
  LET ME LOVE YOU dove però il doppio era sbagliato. Il gate dell'item 57 non
  comprendeva `--phase-lock`.
- [x] Scelta dell'utente: il test si adegua alla regola (non ritardare
  l'ingresso, non tornare al ×2 automatico). `runHeardPhaseLockBench` misura la
  fase sul livello tenuto, in click (a 78 su 156 la parte suona un click sì e
  uno no: è sul pulso), e un controllo nuovo vuole ogni blocco dopo 14 s sul
  pulso o sulla metà tenuta, mai 3:2 o perso. `--phase-lock` **20/0**. 156 a
  78: err -1.5 → **-7.3 ms** (soglia 8; al livello 156 prima -2.8 → -0.6),
  deriva 0.015 click (soglia 0.02). Gli altri quattro tempi invariati.
- [ ] Margine stretto sul 156 tenuto: se torna rosso a caso è questo caso, non
  una regressione nuova; guardare `offLevel` e l'errore tardo.

### 65. Fedeltà dei colpi alla batteria (senza click): misura nuova, quattro candidati respinti 🔴 (2026-09-30, misurato — nessuna modifica al motore)

Richiesta dell'utente: i colpi a volte escono di poco da cassa/rullante e sui
sedicesimi dà fastidio finché non rientrano.
- [x] Misura: `scripts/analysis/onset_fit.py WAV PULSES [--target]`, ora colonne
  `usc/min` e `>25%` di `bench_songs.py`. Attacchi di cassa (40-160 Hz) e
  rullante (160 Hz-4 kHz) contro il sedicesimo del clock più vicino, scarto
  locale mediano su 2 s meno lo scarto tipico del brano; uscita = tratto oltre
  25 ms lungo almeno un battito. `VPTrack --pulses` scrive ora anche `phaseErr`,
  `regime`, `trust` (solo sonda). Onset in cache accanto al wav. Banco
  deterministico: `natfin2`, `fbase`, `fbase2` identici riga per riga.
- [x] Base (24 esecuzioni): **3.48 uscite/min, 8.9% dei colpi oltre 25 ms**.
  Contro la griglia a cui il decoder porta il clock (`--target`): 1.84/min,
  3.5%, meglio in 23 casi su 24. Il margine esiste, ma non è tutto nel follower.
- [x] Attribuzione (secondi di uscita): VIVO fiducia ≥0.5 43%, FISSO fiducia ≥0.5
  33%, fiducia <0.5 solo 15%, CERCO 8%. Due meccanismi visti su THE REASON:
  in FISSO un rallentando lento (~1%) fa crescere l'errore +2 → +29 ms in 7 s
  mentre il trim sale di 0.15 BPM a battito; in VIVO il decoder prolunga
  un'inflessione già finita e la sua griglia anticipa la batteria di ~28 ms.
- [x] Respinto C1 (piccoli errori a fiducia piena su ingresso diretto in ogni
  regime, tau di griglia non allungato): usc/min 3.48 → 3.48, >25% 8.9 → 8.9,
  FEEL 44k aggancio 27.3 → 36.0 s. Matrice product-direct un filo meglio,
  `VPAlign --holes` neutro.
- [x] Respinto C2 (trim al guadagno di moto dopo 3 battiti di deriva concorde su
  ingresso diretto): usc/min 3.48 → **3.69**, scatti 0.92 → 0.96, sbagl s 463 →
  490; `VPAlign --ramps` MIXER FAIL (piatti 100/130 8.2/28.1 → 9.1/31.5 e
  7.1/22.5 → 8.2/25.4, 12 s 35.5 → 27.8). C2b a 5 battiti: piatti rientrano ma
  30 s e 128→120 in FAIL. Il trim non è la leva.
- [x] Misura continua (scarto medio / p90 in ms, stesso banco): clock
  **11.46 / 24.06**, griglia del decoder lisciata 8.75 / 18.44. C1 11.47/24.04,
  C2 11.53/24.17, D1 11.48/24.21: indistinguibili. Il rumore della misura è
  ~8 ms (attacchi dispersi 26 ms, ~20 per finestra), quindi la griglia del
  decoder è già al fondo e il clock ne sta a ~3 ms di media. `|clock − decoder|`
  grezzo è 21.6 ms di media / 49.7 p90: è la dispersione per battito della rete
  (frame da 20 ms), che il clock sta già lisciando quasi quanto una mediana
  centrata su 2 s.
- [x] Respinto D1 (decoder: `kFixedAnchorFloor` 0.02 → 0.10 su ingresso diretto,
  l'ancora del FISSO segue il fit lungo in ~10 battiti invece di 50): usc/min
  3.48 → 3.61, >25% 8.9 → 9.1, scatti 0.92 → 0.86. EVERYTIME 44k scatti 0.90 →
  0.27 e >25% 6.4 → 2.3, ma 48k usc/min 2.84 → 3.90 e VITA scatti 0.37 → 0.46.
  Matrice (4 offset): fisso 22.2 → 22.5, gradino 34.7 → 36.7 ms.
  `probe_tempo_step` PASS.
- [x] Respinto E1 (brano caricato: il mix grezzo dentro `KickOnsetDetector`, colpi
  datati al campione passati al clock tramite `notifyKickOnset`): su THE REASON
  44k il rilevatore è creduto il 97% del tempo, scarto 13.8/28.2 → 14.1/28.6.
- [x] Dove cadono le uscite: 33% del tempo entro 6 s da un cambio di regime
  (quei momenti sono il 24% del totale), 27% con BPM pubblicato che si muove
  oltre l'1%, 41% a regime stabile e BPM fermo. Nessuna classe domina.
- [ ] Prima di altri tentativi: l'utente ascolta alcuni punti indicati da
  `onset_fit.py` (minuto:secondo delle uscite) per dire se sono i momenti che
  dà fastidio sentire. Se sì, si lavora su quella classe; se no, la misura vede
  il feel del brano e non un errore.
- Nota di metodo: `VirtualPercussionEngine.cpp` è CRLF misto: modificarlo a
  byte, mai in modalità testo.

### 66. Scatti di velocità della parte: la piega piena solo su moto provato 🟢 (2026-09-30, misurato — resta ascolto su iPad)

L'utente: i brani sono quasi tutti stabili, ma in certi punti la parte accelera
o frena troppo rispetto all'andamento del brano, anche su un'inflessione leggera
(batterista senza click), e a volte fatica a rientrare.
- [x] Misura: `scripts/analysis/surge_scan.py TAG`, colonna `sc/min` del banco.
  Scatto = clock sentito oltre il 3% dalla sua mediana su 8 s. Base: **91 scatti
  in 85 minuti (1.07/min), 104 s**, 80 in VIVO. Tipico: BPM pubblicato fermo
  (±1-2%), clock a ±6-18% per 1-3 s per chiudere 45-105 ms di fase.
- [x] Erano giustificati? Con gli attacchi di cassa/rullante (`onset_fit.py`)
  prima e dopo: 36 su 83 sì (scarto 26.7 → 11.0 ms), **47 no** (12.7 → 13.9 ms).
  In quei 47 la griglia del decoder si era spostata di colpo e il clock l'ha
  rincorsa.
- [x] Causa: in VIVO il binario piccolo (3%, item 58) valeva solo con errore
  entro 0.06 battiti e "band ferma"; ma in VIVO il target è ritoccato a ogni
  battito e il glide passava per moto, quindi il binario restava al 7.5%.
- [x] Respinto F1 (3% fino a 0.15 battiti, ma ancora con `bandMoving`): scatti
  91 → 85.
- [x] **Tenuto F2** (`TempoFollower::advanceSegment`): in direct-live il binario
  pieno si apre solo con `tempoMotionHint` (moto provato dal decoder); altrimenti
  3% fino a `kLeanIsElsewhere` (0.15 battiti). Banco (24 esecuzioni): scatti
  **91 → 62**, secondi in scatto 104 → 67, scatti% 0.92 → 0.85, nessuna
  esecuzione con più scatti; scarto colpi/batteria 11.46 → 11.50 ms, usc/min
  3.48 → 3.50; aggancio e slittamenti identici; THE REASON 48k fase peggiore
  117.6 → 49.5 ms. Matrice (4 offset): corsia normale identica; product-direct
  fisso 21.51/58.2 → 21.37/58.2, gradino 38.38/168.6 → 38.18/169.6, **continuo
  45.74/116.0 → 49.71/124.2** (il costo: le rampe grandi). `probe_recovery`
  0 FAIL, `probe_tempo_step` PASS, `VPAlign --ramps/--steps` PASS, `VPTests`
  `--phase-lock` 20/0, `--tempo-step` 14/0, `--tempo-slow` 13/0,
  `--tempo-motion` 293/0, `--bar` 10/0, `--new-input` 16/0, `--transport` 4/0.
- [ ] Restano i picchi oltre il 10% (una decina nel banco): errori di fase di
  85-124 ms (oltre 0.15 battiti, quindi "altra griglia") o l'entrata nei primi
  25 s; 6 in FISSO e 5 in CERCO, percorsi non direct-live.
- [ ] Ascolto su iPad: i punti dove prima scattava (es. THE REASON 0:59,
  FEEL 1:01, I WANNA DANCE 3:09, UNA CANZONE 1:42).

### 67. Il trim che spinge contro la fase; brani lenti; STOP/START automatico 🟡 (2026-09-30, T2 tenuto e misurato — il caso "incastrato" non è ancora riprodotto)

L'utente: il problema è soprattutto nei brani lenti (UNA CANZONE PER TE, Sally,
cassa sull'1 e rullante sul 3); quando la parte resta "incastrata" su un altro
pettine, STOP e START la rimettono a posto: si può fare da soli?
- [x] UNA CANZONE PER TE 48k, 67 s: il decoder è quasi fermo (86.0 → 87.1 → 85.0)
  ma un colpo spostato muove la fase di 0.07 battiti in un battito; il trim prende
  +2.46 BPM in una osservazione (guadagno di moto, `ioiLead`) e resta +2.2/+1.1
  per 8 s mentre la parte è già 85 ms in anticipo: una deriva di segno opposto
  vale zero finché tre non concordano. È "accelera troppo e fatica a rientrare".
- [x] **Tenuto T2** (`TempoFollower::observeOnsetPhase`, solo ingresso diretto):
  se il trim spinge nel verso dell'errore di fase (oltre 0.08 battiti) viene
  dimezzato a ogni osservazione. Episodio: trim +2.46 → +0.95 → +0.48 → +0.11.
  Banco: scarto colpi/batteria 11.50/24.13 → **11.33/23.70 ms**, usc/min 3.50 →
  3.41, >25% 9.1 → 8.7, scatti 62 → 63 (FEEL 48k e SPLENDIDA 44k +1), aggancio
  e slittamenti identici. `VPAlign --ramps` PASS e pari entro 0.3 ms (a 0.04 e
  0.06 battiti fallisce la media del 128 → 120). Matrice identica in entrambe
  le corsie (non usano la guardia di direzione). `VPTests` `--phase-lock` 20/0,
  `--tempo-step` 14/0, `--tempo-slow` 13/0, `--tempo-motion` 293/0, `--bar`,
  `--new-input`, `--transport`; `probe_recovery` 0 FAIL.
- [x] Respinto T1 (tetto a quanto una osservazione può dire al trim): 2% e 3%
  fanno fallire la rampa di 12 s (35.5 → 39.2/38.3); 4% passa a 37.7 ma FEEL 44k
  aggancia a 36.0 s invece di 27.3.
- [x] Respinto G1 (sciogliere la tenuta della Door D quando l'intervallo torna
  indietro): non tocca l'episodio (non era la porta) e il continuo sintetico
  sale 44.17 → 44.45.
- [x] Silenziare o fermare quando l'app si crede fuori sync: no con i segnali di
  oggi. Errore clock/decoder oltre 0.15 battiti per due battiti: 42 volte l'ora,
  confermato dalla batteria 8 volte su 66. Sally 2:00-2:22: clock entro ±25 ms
  dalla batteria mentre i battiti accettati stanno a +80/+116 ms dal clock.
- [x] STOP/START simulato (`VPTrack --stop-at T`, nuovo): BLUE SKY 48k a 74 s e
  Sally a 130 s non migliorano (Sally peggiora di ~30 ms per 6 s). I tratti
  trovati dalle misure non sono il caso che l'utente corregge a mano.
- [ ] Serve un caso vero: brano e minuto in cui l'utente fa STOP/START e rientra,
  oppure un registro nell'app di ogni STOP → START ravvicinato (posizione nel
  file, BPM e fase prima e dopo) per vedere in che stato era il clock.
- Sally (Flamingo 10:10-14:50, `extract_live.swift ... 610 280`) è ora in
  `/tmp/vp-bench/wav/99_SALLY_LIVE_48k.wav`: 3 scatti in 4.7 minuti.

### 68. Salti falsi della griglia in FISSO: due battiti prima di crederci 🟢 (2026-09-30, misurato — resta ascolto su iPad)

Prova chiesta dall'utente: far ignorare al clock i salti grandi non confermati.
- [x] Respinto H1 (VIVO: oltre 0.15 battiti la piega piena solo dopo due battiti
  dallo stesso lato): scatti 63 → 62, scarto 11.33 → 11.32 ms, Sally invariata;
  matrice product-direct continuo 49.7 → 51.2, gradino 38.2 → 39.4. I picchi
  rimasti non passano da lì.
- [x] Causa vista su EVERYTIME 48k a 113.8 s: FISSO fermo a 123.4 BPM, la griglia
  del decoder fa un passo falso di ~0.2 battiti per mezzo secondo; l'obiettivo
  lontano viene adottato dopo 0.25 s e il tetto di sterzo si apre con l'errore: il
  clock frena a 104 BPM (−16%), si trova 0.21 battiti indietro e corre a 131 per
  1.5 s.
- [x] **Tenuto** (`TempoFollower::setFixedDirectFeed`, `BeatTracker`): su ingresso
  diretto, con il decoder in FISSO sulla stessa griglia da almeno 8 s e la parte
  che suona, un obiettivo di fase lontano deve restare dallo stesso lato per due
  battiti prima di (a) accorciare la media di fase e (b) aprire il tetto oltre il
  limite ordinario. Episodio: clock 103.6–132.3 → **117.3–126.8 BPM**. Banco:
  cambia solo EVERYTIME 48k (scatti 4 → 3, picco 17.0 → 5.8%, scatti% 1.28 →
  0.80, sbagl s 32.4 → 31.2); le altre 23 esecuzioni e Sally senza differenze
  nelle misure. Media scatti% 0.85 → 0.83.
- [x] Senza l'anzianità di 8 s (solo "regime FISSO"): `VPTests --bar` perdeva
  "seek re-aligns the one on the new downbeats" 2 volte su 8 (12/12 sul commit):
  dopo un seek l'ipotesi vecchia dice ancora FISSO. Con l'anzianità 12/12.
  Senza la restrizione al FISSO (guardia su tutto l'ingresso diretto): FEEL 44k
  aggancio 27.3 → 35.8 s, LET ME LOVE YOU 48k 23.8 → 28.7 s.
- [x] Gate: `VPAlign --ramps/--steps` PASS, matrice identica in entrambe le corsie
  (le sonde non accendono la guardia), `probe_recovery` 0 FAIL, `probe_tempo_step`
  PASS, `VPTests` `--phase-lock` 20/0, `--tempo-step` 14/0, `--tempo-slow` 13/0,
  `--tempo-motion` 293/0, `--bar` 10/0, `--new-input` 16/0, `--transport` 4/0.
- [ ] Restano i picchi oltre il 10% in VIVO (BLUE SKY, FEEL, INFINITO, SPLENDIDA,
  EVERYTIME 44k 2:41) e all'entrata: meccanismo non ancora individuato.
- [ ] Ascolto su iPad: EVERYTIME a 1:53.

### 69. Da dove vengono gli scatti rimasti; un gradino poco sicuro non fa saltare il clock 🟢 (2026-09-30, misurato — resta ascolto su iPad)

Richiesta: vedere se si può stabilizzare ancora il tempo in generale.
- [x] Strumento: `VPTrack --pulses` scrive anche `rete target trim trans recover
  bridge`; `scripts/analysis/surge_sources.py TAG` scompone ogni scatto. Banco
  (24 esecuzioni + Sally), 65 scatti: piega di fase 43 (45.8 s, quasi tutti
  3.9–5%: il 3% dell'item 66 più trim e decoder), **transizione 12 (12.5 s, i
  picchi dal 6 al 26%)**, decoder 8 (12.7 s), trim 2.
- [x] Respinto R1 (binario della finestra rapida proporzionale al gradino, 3×):
  byte per byte identico. Lo scatto non è la spesa di fase: è il tempo messo
  subito sul valore dichiarato.
- [x] Registro delle transizioni passate al clock: INFINITO (91 fermo) 90.9 →
  84.9 confidenza 0.42; FEEL (104.5) 108.3 → 100.9 conf 0.54; BLUE SKY 87.5 →
  84.3 conf 0.42; SPLENDIDA (108) 105.8 → 115.2 conf 1.00. Gradini falsi
  confermati dal decoder su intervalli che concordano poco.
- [x] Respinto S1 (ogni gradino sotto il 9.5% come obiettivo ordinario):
  `VPAlign --steps` 5 FAIL (118→124, 118→128, 128→120, 76→82, 168→156: ±1 BPM in
  1.46–1.97 s invece di 0.78–1.47, 47–87 ms al terzo battito invece di 24).
- [x] **Tenuto S2** (`BeatTracker::process`, `kTransitionJumpConfidence` 0.75 in
  `PhaseTrust.h`, solo ingresso diretto): `beginTempoTransition` solo se la
  confidenza della transizione è almeno 0.75; sotto, il tempo nuovo è un
  obiettivo ordinario. I 25 gradini veri di `VPAlign` leggono 0.89–1.00:
  `--steps` identico riga per riga, `--ramps` PASS. Banco: scatti 65 → 62,
  transizioni 12 → 8 (12.5 → 5.7 s), scatti% 0.83 → **0.79**; INFINITO 44k/48k
  scatti 1 → 0 e scatti% 0.52/0.57 → 0.31; FEEL 44k picco 12.5 → 6.1%; BLUE SKY
  48k scatti% 1.12 → 0.91. Scarto colpi/batteria 11.35 → 11.36 ms, aggancio e
  slittamenti identici. Piccoli costi: BLUE SKY 48k sbagl s 15.1 → 16.1, FEEL 44k
  fase peggiore 40.2 → 42.5 ms.
- [x] `VPTests` `--tempo-step` 14/0, `--tempo-slow` 13/0, `--tempo-motion` 293/0,
  `--bar` 10/0 (6 volte su 6), `--new-input` 16/0, `--transport` 4/0.
  `--phase-lock`: **19/1 una volta**, subito dopo il banco con la macchina
  carica, poi 20/0 tre volte su tre con numeri identici; il controllo caduto
  non è stato visto.
- [ ] Restano: SPLENDIDA 0:47 (+12%, transizione a confidenza 1.00), BLUE SKY
  2:35 (−11% per 0.2 s in FISSO), EVERYTIME 44k 2:41 (+19%: nessuna transizione
  passata al clock, la griglia salta di 0.3 battiti durante lo stato di
  transizione e il percorso non direct-live apre il tetto), l'entrata nei primi
  25 s (ASPETTANDO +26%, LET ME LOVE YOU −24%), Sally 1:51 (decoder −13% in
  CERCO).
- [ ] Ascolto su iPad: INFINITO verso 4:43, FEEL verso 3:26.

### 70. Verso un salto di qualità: l'aggancio all'attacco non serve; registro degli STOP/START 🟡 (2026-09-30 — serve una prova dell'utente)

Vincolo dell'utente: dal vivo all'iPad arriva solo il mix completo (un canale
cassa separato è troppo macchinoso). Piano concordato: registro degli STOP/START
e datazione dei battiti sull'attacco; poi, eventualmente, una rete migliore.
- [x] **Respinto prima di toccare il motore: datare ogni battito sull'attacco più
  vicino nel mix.** Battiti accettati dalla rete su sei brani a 48 kHz (stampa
  temporanea, poi tolta), irregolarità degli intervalli (mediana/p90 ms): VITA
  10.9/29.9, EVERYTIME 6.8/19.7, THE REASON 8.9/21.4, I WANNA DANCE 7.9/22.5,
  UNA CANZONE 13.2/40.0, Sally live 20.1/55.4. Agganciati all'attacco entro
  30 ms: 12.9/38.5, 7.7/26.5, 7.9/23.9, 10.2/31.1, 16.2/48.4, 18.1/67.1 —
  peggio in cinque su sei. Anche a 12, 20, 45 ms e con i soli bassi (40–160 Hz)
  o i soli medio-alti (1.5–8 kHz). Nel mix l'attacco più vicino a un battito
  spesso non è la batteria; la rete è già più regolare.
- [x] **Registro degli STOP/START** (diagnostica, il comportamento del motore è
  identico byte per byte): `EngineSnapshot::silentSnapBeats/silentSnapCount`
  (di quanto il clock è stato posato in silenzio, `BeatTracker`), e in
  `MainComponent` a ogni STOP seguito da START entro 10 s una riga, 4 s dopo lo
  START, in `Documents/VirtualPercussionist-stopstart.log`: brano e posizione,
  durata della pausa, stato PRIMA e DOPO (bpm, clock, rete, pettine, regime,
  errore di fase, fiducia, battuta) e di quanto è stato riallineato. La
  cartella è visibile in File ("Su iPad") con `FILE_SHARING_ENABLED` e
  `DOCUMENT_BROWSER_ENABLED`. `VPTrack` stampa i riallineamenti a fine corsa:
  Sally con `--stop-at 130` 3 riallineamenti, ultimo +0.051 battiti; senza, 1.
  Build iPadOS Debug senza firma riuscita; `VPTests` `--tempo-step` 14/0,
  `--bar` 10/0, `--new-input` 16/0, `--transport` 4/0.
- [ ] L'utente prova sull'iPad: quando sente la parte "incastrata" fa STOP e
  START come sempre, poi manda il file. Con i casi veri si costruisce il
  riconoscimento per farlo da soli.
- [ ] Prova al computer di una rete migliore (dispersione e falsi nei fill
  sugli stessi brani) prima di qualunque integrazione.

### 71. Altri pesi di BeatNet; il registro dell'utente: la parte corre sopra il pettine 🔴 (2026-09-30, diagnosi misurata — nessuna correzione pulita)

- [x] **Pesi alternativi, respinti.** `model_1` (GTZAN) è il modello dell'app
  (esportazione identica byte per byte). `model_2` (Ballroom) e `model_3` (Rock
  Corpus) scaricati in `third_party/beatnet-weights`, esportati fuori
  dall'albero e provati sul banco intero con `VP_BEAT_MODEL` (che ora ha la
  precedenza sul modello incorporato, `ModelLocator.cpp`). GTZAN / Ballroom /
  Rock: scarto colpi 11.36 / 12.13 / 11.76 ms, >25 ms 8.7 / 11.3 / 10.2 %,
  aggancio 13.1 / 17.3 / 19.0 s, sbagl s 465 / 401 / 746, scatti 62 / 44 / 63,
  scatti% 0.81 / 0.88 / 0.96, slittamenti 0 / 0 / 1. Nessuno dei due è meglio.
- [x] **Registro dell'utente** (`VirtualPercussionist-stopstart.log`, Flamingo
  64:24–66:07, cinque STOP/START): riallineamenti di soli 20–34 ms (la griglia
  non era spostata); in quattro su cinque il tempo dell'app era 1–2% sopra il
  pettine e dopo ci torna (128.1 → 126.1 con pettine 125.5; 126.2 → 124.5).
- [x] Riprodotto (`/tmp/vp-bench/wav/98_FLAMINGO_3750_48k.wav`, estratto
  3750–4000 s): a 108–122 s la band sale 121 → 125 e resta; fit corto 129,
  rete 127–128.5, clock fino a 130, pettine 125 per tutto il tratto. Tempo
  dagli attacchi (autocorrelazione su 6 s): 125–126. L'app va 2–4% più veloce
  per ~8 s e rientra da sola. Sul tratto intero rete oltre l'1.5% dal pettine
  7.2% del tempo, clock oltre il 3% 7.4%.
- [x] Sei varianti della stessa idea (più autorità al pettine in VIVO su ingresso
  diretto), **tutte respinte**:
  - V1 niente lead quando punta lontano dal pettine (>1.5%): effetto piccolo.
  - V2 tirata 60% da 1.5% sul target: Flamingo rete oltre 1.5% 7.2 → 1.8%, clock
    max 21 → 4.4%; ma `VPAlign --ramps` FAIL (12 s 35.5 → 41.9), continuo
    44.2 → 47.3.
  - V3 come V2 solo con pettine fermo su 4 battiti (0.5%): rampe quasi a posto
    ma beneficio sul Flamingo perso (4.0%, clock max 21%).
  - V4 tirata 60% quando è il fit corto a stare >1.5% dal pettine, più V1:
    Flamingo 2.0% / clock oltre 3% 4.5%; rampe 12 s 37.9 e 120→132 28.3 FAIL;
    banco misto: BLUE SKY 48k scatti 6 → 2, ASPETTANDO aggancio 25.4 → 18.8 s,
    ma I WANNA DANCE 44k scatti 0 → 2 (picco 11.9%), FEEL 44k aggancio 27.3 →
    36.0 s, EVERYTIME 48k fase peggiore 119 → 243 ms; scarto 11.50 → 11.63.
  - V5 come V4 al 35%: rampe FAIL uguali, Flamingo peggio della base (13.5%).
  - V6 come V4 senza V1: una rampa FAIL (120→132 27.7), Flamingo peggio (12.4%).
- [ ] Da capire prima di riprovare: perché il fit corto legge 129 con la band a
  125 (i battiti accettati scivolano in avanti sotto una griglia che li
  insegue: l'eccesso si autoalimenta, con V2 il fit corto torna a 124–126).
  La leva è forse l'accettazione dei picchi sotto una griglia che accelera,
  non il bersaglio.

### 72. Anticipi presi per battiti: la riapertura dopo un buco, in VIVO, non accetta più un sedicesimo in anticipo 🟡 (2026-09-30, misurato — banchi finali fatti, manca la ricompilazione dopo il solo commento e l'ascolto)

Seguito dell'item 71, sulla strada indicata lì (quali picchi vengono accettati).
- [x] Causa, picco per picco (stampa temporanea, poi tolta): dal 64:17 del
  Flamingo la band suona un colpo forte un sedicesimo prima del battito ogni due
  battiti (−0.20/−0.27 battiti). Il decoder li scarta (fuori da 0.18), ma due
  volte li accetta dalla **riapertura dopo un buco** (`observe`, ramo
  `sounding && beats >= kGridStaleBeats`): dopo 2.5 battiti senza battiti
  accettati prende un picco sul pettine fino a 0.40 battiti dall'ultimo. A
  111.32 s (−0.198) e 115.49 s (−0.267): ogni volta `lastBeat` va un quinto di
  battito indietro, il fit corto legge 129 e la parte corre.
- [x] Le riaperture sono rare (2–9 per brano) ma ognuna sposta la griglia di
  0.18–0.40 battiti. W1 (tolleranza da 0.18 a 2.5 battiti fino a 0.40 a 8
  battiti, sempre): Flamingo e Sally meglio, ma UNA CANZONE 48k aggancia a 33.4 s
  invece di 15.4 e 44k fa due scatti nuovi (picco 19.6%): le sue tre riaperture
  sono tutte in CERCO e le servono.
- [x] W2 (stretta solo con tempo stabilito, FISSO o VIVO): UNA e FEEL tornano
  identici, Sally scatti% 1.26 → 0.95 e picco 13 → 5%; ma EVERYTIME 48k slitta
  di un battito due volte (le riaperture a 98, 102, 140 s in FISSO le servono
  dopo il fill).
- [x] W3 (stretta solo in VIVO): niente slittamenti, Flamingo e Sally come W2,
  ma EVERYTIME 48k scatti% 0.80 → 0.98 e 44k picco 18.8 → 22.9%.
- [x] **Tenuto W4** (`BeatDecoder::observe`, `kReopenFullBeats` 8): stretta solo
  in VIVO e solo per un picco **in anticipo**. Flamingo: tempo a 64:19–64:27
  124.6–126.9 invece di 127–129, scatti% 1.28 → 1.19, >25 ms 20.5 → 17.0%,
  usc/min 6.96 → 5.92, slittamenti 1 → 0. EVERYTIME, UNA CANZONE, FEEL e gli
  altri identici nelle misure; Sally torna alla base (>25 ms 9.7 → 10.2): lì
  il picco dannoso era in ritardo, e i ritardi servono a EVERYTIME. Media del
  banco: scatti% 0.83 → 0.82, slittamenti 1 → 0, fase verificata 18.3/43.1 →
  14.9/33.9 ms. Banchi sintetici identici (la matrice non accende `sounding`),
  `VPAlign --ramps` PASS, `--steps` identico, `probe_tempo_step` PASS,
  `VPTests` `--tempo-step` 14/0, `--tempo-slow` 13/0, `--tempo-motion` 293/0,
  `--bar` 10/0, `--new-input` 16/0, `--transport` 4/0, `--phase-lock` 20/0.
- [ ] Dopo questi banchi è cambiato solo il commento nel codice: ricompilare e
  ripetere un controllo rapido prima del commit.
- [ ] Aperto: Sally con W3 migliorava molto; serve un criterio che distingua il
  ritardo dannoso di Sally da quelli utili di EVERYTIME (il verso non basta).
- [ ] Ascolto su iPad, sul live: il punto 64:19 del Flamingo; e continuare a
  usare il registro degli STOP/START.

### 73. Griglia spostata sotto la parte: 21 quarti buoni rifiutati di fila (I WANNA DANCE, finale) 🟡 (2026-09-30, misurato — manca l'ascolto)

Segnalazione: "i brani seguono leggermente peggio forse… alla fine soprattutto esce molto" su I WANNA DANCE.
- [x] Non è una regressione di oggi: su questo brano i banchi da `s2` (13:58) a `w4` sono uguali, e il tratto
  c'era già in `fbase` (29/09). Il brano è fermo a ~124.4 (tempogramma indipendente).
- [x] Causa, picco per picco (stampa temporanea, poi tolta), 44.1k: a 169.5 s FISSO→VIVO e il tempo sale a
  127.8 (classe dell'item 71, aperta). A 178.88 s `lastBeat` resta su un picco debole (0.65). Da 179.41 a
  189.61 s arrivano 21 quarti regolari (0.485 s, cassa, forza 0.85–1.00) e sono **tutti rifiutati**: il primo
  dalla regola dei rulli (1.111), poi fuori da 0.18, poi fuori dal pettine di un tempo che non è quello del
  brano. Confidenza 0 per 8 s, la parte al 3% in più per 10 s (~0.6 battiti).
- [x] Stessa classe su altri brani: in VIVO con la parte che suona, 14 tratti ≥4 s senza battiti accettati
  nel banco (117 s in tutto); non tutti sono questo caso (alcuni sono pause vere).
- [x] **Tenuto** (`BeatDecoder::observe`, `kRefusedRunBeats` 4): quattro picchi rifiutati di fila, con cassa,
  ognuno a un periodo (entro 8.7%) dal precedente, nessun battito accettato in mezzo e non nella zona del
  mezzo battito (< 0.32) → il quarto viene accettato. Muto con banda bassa 0 (banchi sintetici, fixture).
  I WANNA DANCE 44.1k: buco 10.2 → 1.5 s, secondi sul tempo sbagliato 35.8 → 23.4, >25 ms 5.5 → 4.6%.
  Banco (26 esecuzioni, `w4` → `x1`): 21 identiche; sbagl s 464.9 → 450.5, usc/min 3.58 → 3.55, >25 ms
  9.1 → 8.9%, slittamenti 0. Costo: I WANNA DANCE 48k scatti% 0.73 → 0.83 (media 0.82 → 0.83).
  `VPAlign --ramps` PASS (stessi numeri), `--steps` 0 FAIL, `VPTests --tempo-step` 14/0, `--tempo-slow`
  13/0, `--new-input` 16/0, `--transport` 4/0, `--bar` 10/0 in 12 esecuzioni su 13 (una 9/1, asserzione non
  catturata: da rivedere se si ripete).
- [ ] Non eseguiti dopo questa modifica: `probe_tempo_step`, `probe_motion_matrix` (per costruzione non
  accendono `sounding` e passano banda bassa 0), fixture hats A/B, rulli e D.
- [ ] Aperto: al rientro il clock frena fino a 117 per ~2 s (44.1k, 187 s) e il tempo scende a 122 prima di
  tornare a 123.7; e la salita a 128 a 169 s che origina tutto resta (item 71).
- [ ] Ascolto su iPad del finale di I WANNA DANCE.

### 74. Sessioni lunghe: la proiezione dell'analisi si fermava a 0.6 s 🟡 (2026-10-01, misurato — manca il riscontro su iPad)

Segnalazione: su tracce lunghe o con START tenuto a lungo la parte comincia a sfasare con il pallino al 100.
- [x] `BeatTracker::process` proietta la fase dell'analisi in avanti del ritardo misurato (FIFO + finestra +
  latenza), ma con un tetto a 0.60 s presente dal primo commit, senza misura. Se il worker resta indietro
  (iPad caldo, set lungo) oltre quel tetto la parte suona in ritardo dell'eccesso e la confidenza resta piena.
- [x] `VPTrack --lag SEC` tiene il worker indietro di SEC. Prima: con 1 s VITA e INFINITO perdono tutta la fase
  verificata, colpi oltre 25 ms 5.5 → 15.7% e 1.4 → 27.4% (2 s: 24.1 e 22.4%). Dopo (tetto 10 s, la FIFO):
  1 s 6.7 e 1.9%, 2 s 6.4 e 4.6%, fase verificata 181–204 s. Ritardo 0 identico.
- [x] `VPTests --phase-lock` 20/0, `--new-input` 16/0, `--state-timing` 0 FAIL, `--tempo-step` 14/0,
  `--transport` 4/0, `--bar` 10/0.
- [ ] Su iPad: il pannello DEBUG mostra `lead` (ora non più tagliato). Normale ~70–120 ms; se cresce durante
  il set, l'analisi non sta al passo e va cercato il perché (CPU, CoreML, termica).
- [x] Flamingo intero (97 min, `VPTrack --player`, worker in passo): nessuna deriva col tempo. Scarto mediano
  cassa/rullante contro la parte per finestre di 5 minuti fra +0.7 e +9.9 ms senza tendenza; colpi oltre
  25 ms fra 1.7 e 15.4% secondo il materiale, non crescenti. 4 restart dell'analisi (17.5, 603, 1511, 5504 s).
  Quindi lo sfasamento che cresce nel set non viene dall'algoritmo: sul desktop non c'è; su iPad la causa
  plausibile è l'analisi che resta indietro, corretta qui sopra.
- [x] `onset_fit.py` legge i file a pezzi da 60 s: sull'intero set calcolava uno spettrogramma da ~30 GB e ha
  bloccato il Mac.

### 75. Tenuta del tempo nel tracker (banda attorno al tempo lento del brano): scartata 🔴 (2026-10-01)

Priorità dell'utente: un batterista non crolla né accelera di colpo. Sul banco, dopo l'aggancio, la parte sta
oltre il 2% dal tempo del brano il 16% del tempo (751 s su 4734, brani da studio).
- [x] Prova: il tempo passato al clock resta entro ±c del tempo lento (media 30 s) dopo 8 s di parte; fuori
  dalla banda per N s dallo stesso lato → creduto. Fermo dello sterzo di fase mentre tiene.
- [x] c 1.5% N 10 s / c 1% N 16 s: oltre il 4% 167 → 50 / 34 s, scatti% 0.87 → 0.73 / 0.56, sc/min 0.84 →
  0.41 / 0.24, sbagl s 448 → 420 / 367. Ma colpi >25 ms 9.9 → 10.8 / 12.2%, sb<40 111 → 160 / 196 s,
  aggancio 13.1 → 15.6 / 17.2 s, **slittamenti di un battito 0 → 3 / 1**, Sally live >25 ms 10.2 → 15.7%.
  Tenendo il tempo mentre la griglia del decoder si muove, la fase scappa. Tolta dal tracker (era nel commit
  b7b72d6 dietro variabili d'ambiente, spenta).
- [ ] La tenuta va cercata nel decoder (non lasciare FISSO/non salire su prove deboli), non a valle sul clock.

### 76. Tenuta nel decoder: limite di velocità del tempo in VIVO 🔴 (2026-10-01, misurato, scartato)

Dopo l'item 75. Uscite oltre il 2% sul banco (brani da studio, dopo l'aggancio), per origine: decoder già in
VIVO 220 s, rilascio FISSO→VIVO 170 s, clock che insegue la fase 220 s, il resto in FISSO/CERCO.
- [x] Prova (`BeatDecoder::updateTempo`, ramo `live`, solo `lineFeed`, parte che suona, nessuna transizione o
  refit): il tempo committed cambia al massimo di `slew` per battito accettato.
- [x] Banco ricostruito (30 esecuzioni, 4 alla volta). Base → 0.25% → 0.15% per battito: oltre il 2%
  751 → 662 → 629 s, oltre il 4% 167 → 108 → 88 s, sbagl s 448 → 369 → 353, scatti% 0.87 → 0.77 → 0.80,
  sc/min 0.84 → 0.48 → 0.57. Ma colpi >25 ms 9.9 → 10.4 → 10.4%, fase verificata media 12.9 → 19.8 → 23.0 ms,
  e peggiorano singoli brani: SPLENDIDA 48k >25 ms 1.7 → 8.7%, Sally live 10.2 → 13.0% (0.25%), ASPETTANDO
  48k uno slittamento con 0.15%. Il calo (`dip`) identico. Non tenuto: migliora il ritmo, peggiora dove cadono
  i colpi su più brani.
- [ ] `line_scan` non è un confronto stabile qui: i secondi verificati cambiano da 0 a 57–90 tra le varianti
  (1000 GIORNI), quindi gli "slittamenti" nuovi stanno in tratti prima non verificati.
- [x] Tenuta legata alla qualità delle prove: stesso limite (0.15%/battito) solo quando la mossa è debole.
  Variante 1 = battiti messi peggio del brano (`placementTrust` < 0.50) **o** pettine che non segue la mossa
  (almeno un quarto, stesso verso); variante 2 = solo battiti messi male. Base → v1 → v2: oltre il 2% 751 →
  704 → 715 s, oltre il 4% 167 → 136 → 149 s, sbagl s 448 → 400 → 421, sc/min 0.84 → 0.77 → 0.86, >25 ms
  9.9 → 10.1 → 10.1%. Peggiorano 6 brani in entrambe (v1: SPLENDIDA 48k >25 ms 1.7 → 7.2%, UMBRELLA 48k
  19.0 → 26.9%, LET ME LOVE YOU 48k 9.8 → 12.5%; v2: Sally live 10.2 → 13.4%, I WANNA DANCE 44k 4.3 → 7.6%).
  **Codice riportato com'era** (richiesta dell'utente se non migliora).
- [ ] Conclusione di 75–76: frenare il tempo, a valle o nel decoder, con o senza prove, scambia velocità stabile
  con colpi meno allineati. La causa da attaccare è a monte: perché il decoder legge 3% in più su prove che il
  pettine e i battiti non confermano (item 71).

### 77. La parte resta fuori dalla griglia dell'analisi: ricentro come STOP/START, in ascolto 🟡 (2026-10-01)

Segnalazione dal set (Uptown Funk, Another One Bites the Dust, Billie Jean, Get Lucky, Flamingo ~58–82 min):
le percussioni non stanno al centro e restano sfasate; STOP/START le ricentra subito.
- [x] Log `VPLAG` da iPad: `lead` 38–66 ms, coda ≤16 ms, 0 buchi: l'analisi tiene il passo, non è l'item 74.
- [x] Tempo dagli attacchi di cassa/rullante contro il tempo pubblicato, finestre di 10 s: 1–2 BPM di scarto.
  Il clock sentito invece in 10 s spazia per esempio 112–121 BPM (picchi a 146 e 152).
- [x] Contro la batteria vera la griglia dell'analisi è molto meglio del clock: colpi oltre 25 ms 4.2% contro
  9.9% (30 esecuzioni del banco), 3.7% contro 10.9% sul Flamingo intero. Il clock resta ≥0.08 battiti fuori
  dalla griglia per ≥4 s 69 volte (807 s su 97 min), in 36 con un buco di battiti accettati dentro, in 29
  dopo un cambio confermato. Esempio 76:00: falso cambio 122.8 → 128.1 (batterista ~124), clock 0.2 battiti
  avanti, `beatGapHold` ferma la correzione per 5 s, poi la lean chiude in 10 s.
- [x] `BeatTracker::process`: se la parte suona e il clock resta oltre 0.06 battiti dalla griglia pubblicata
  dallo stesso lato per 2 battiti, `snapPhase` di al massimo 0.20 battiti (lo stesso tetto del nudge che c'è
  già alla ricostruzione della griglia). Banco: >25 ms 9.9 → 9.0%, usc/min 3.82 → 3.53, sc/min 0.84 → 0.75;
  I WANNA DANCE 48k 11.2 → 4.7%, EVERYTIME 44k 5.3 → 1.5%, Sally 10.2 → 7.4%, Flamingo 17.0 → 14.9%.
  Peggiora UMBRELLA 48k 19.0 → 25.3% con **un solo** ricentro in tutto il brano (dato fragile). 443 ricentri
  in 30 esecuzioni (1000 GIORNI 60 senza guadagno). Più prudenti, 0.06 per 4 battiti e 0.08 per 3: 9.2 e
  9.3%, 299 e 278 ricentri, peggiorano BLUE SKY 48k e UNA CANZONE 44k. Tenuta 0.06/2 come candidata.
- [x] `VPTests --phase-lock` 20/0, `--bar` 10/0, `--new-input` 16/0, `--transport` 4/0, `--tempo-step` 14/0,
  `--state-timing` 0 FAIL. `VPLAG` stampa anche `ricentri` (`phaseNudgeCount`).
- [ ] Ascolto su iPad dello stesso tratto del Flamingo: si sentono i ricentri? la parte sta al centro?

### 78. I pallini dei quarti si accendevano prima del colpo e a scatti 🟡 (2026-10-01, da verificare a occhio)

- [x] Il pallino era deciso dal timer UI a 15 Hz su `snap.barPhase`, cioè la posizione del clock che si sta
  *calcolando*: si accendeva prima del colpo sentito di tutta la latenza d'uscita (buffer + dispositivo; con le
  AirPods ~150–250 ms) e dopo il momento giusto di 0–67 ms, diverso a ogni battito.
- [x] `MainComponent::updateBeatDots` su `juce::VBlankAttachment` (ogni fotogramma dello schermo): fase del clock
  meno (latenza d'uscita + `attackLeadMs`), ridisegna solo la striscia dei pallini quando il quarto cambia.
  Vale in BRANO (la canzone esce dalla stessa uscita) e dal vivo (la band è già nella stanza).
  `VPLAG` stampa `uscita` (latenza d'uscita usata).
- [x] Verificato: la latenza delle AirPods non entra nel tempo della parte in BRANO (`roundTrip = 0` per
  `internalPlayer`, `VirtualPercussionEngine.cpp`).
- [ ] Verifica a occhio su iPad, con AirPods e con cavo/mixer.

### 79. Falsi salti dopo l'uscita da FISSO: il recupero veloce solo se il pettine lo conferma ✅ (2026-10-01, misurato — manca l'ascolto)

- [x] Flamingo intero: 43 salti del tempo pubblicato di almeno il 3% in un secondo con la parte che suona,
  quasi tutti in VIVO; per circa metà i colpi degli 8 s dopo stanno meglio sul tempo vecchio, e spesso il
  tempo torna indietro in pochi secondi (61:04 121.8 → 115.1 → 121.0).
- [x] Traccia temporanea in `updateTempo` sull'estratto 62–66 min: i tre salti (64:01, 65:15, 65:45) sono la
  prima battuta dopo un rilascio FISSO→VIVO, ramo `far` (`kRateAcquiring` 0.70) verso una retta corta rumorosa
  con il pettine più vicino al tempo tenuto (64:01: tenuto 124.2, bersaglio 120.4, pettine 122.7, batterista
  122.8).
- [x] `BeatDecoder::updateTempo`, ramo `live`: con la parte che suona su ingresso diretto, `far` resta solo se il
  pettine è più vicino al bersaglio che al tempo tenuto. Sull'estratto restano 1 salto su 3 (quello confermato
  dal pettine). Banco (30 esecuzioni) contro la versione con i ricentri: >25 ms 8.96 → 8.92%, usc/min 3.53 →
  3.48, oltre il 4% dal tempo del brano 146 → 138 s, oltre il 2% 744 → 742 s, nessun brano peggiore.
  I banchi sintetici non accendono `sounding`. `VPTests` `--phase-lock` 20/0, `--bar` 10/0, `--new-input`
  16/0, `--transport` 4/0, `--tempo-step` 14/0, `--tempo-slow` 13/0, `--state-timing` 0 FAIL.
- [x] Inseguimento più morbido (lean costante 3% → 1.5%, con e senza ricentro a 0.05): scatti 0.75 → 0.60/0.50,
  oscillazione del clock 0.93 → 0.86/0.84%, ma >25 ms 8.96 → 9.08/8.88 e 2/6 brani peggiori. Non tenuto.
- [x] Tolti gli interruttori sperimentali `VP_NUDGE`/`VP_LEAN` finiti nel commit 8809a7d (getenv sul percorso
  audio).
- [ ] L'altra metà dei salti falsi non viene da questo ramo: da cercare allo stesso modo sul resto del set.

### 80. Glitch nei brani caricati: il thread audio aspettava il decoder dell'mp3 🟡 (2026-10-01, da ascoltare)

- [x] Log `VPLAG` da iPad in Release, AirPods: `xrun` 3 → 7 in un minuto, `callback` 0.02–0.03 ms (su 5.3 ms),
  `tagli` 0, `vuoti brano` 0. I glitch sono blocchi audio consegnati in ritardo, non il carico dell'app.
- [x] Causa nel codice: `juce::AudioTransportSource` legge con `BufferingAudioSource`, che tiene `callbackLock`
  mentre decodifica un pezzo di file (`juce_BufferingAudioSource.cpp:332`) e lo prende anche nel callback audio
  (`:127`). Se il thread di lettura viene sospeso a metà decodifica, il thread audio aspetta.
- [x] `Source/Audio/TrackStreamer.h`: il thread di lettura decodifica in un anello SPSC senza lock; il thread
  audio legge e ricampiona (Lagrange) senza mai aspettare; ricerca e cambio file passano per uno `SpinLock`
  che il thread audio prende solo in try-lock (un blocco di silenzio invece di un'attesa). Stessi nomi di
  metodo di `AudioTransportSource`. Anello 2^18 frame (~6 s), letture da 4096.
- [x] `scripts/probe_streamer.cpp` (via `VPStyle`): 20 s a ~5× tempo reale su file 44.1 e 48 kHz, uscita
  identica bit per bit a lettura diretta + stesso interpolatore, 0 silenzi, 0 underrun, seek a 30 s corretto.
- [ ] Il banco `VPTrack --player` legge il WAV direttamente, non passa per questo lettore: invariato.
  Sull'iPad cambia il ricampionatore del brano (prima `ResamplingAudioSource`, ora Lagrange).
- [ ] Ascolto in Release su iPad: `xrun` deve smettere di salire durante i brani caricati.

### 81. Falsi cambi a gradino confermati sotto la parte; ricentro più rapido e morbido scartati ✅ (2026-10-01, misurato — manca l'ascolto)

- [x] Flamingo intero dopo l'item 79, traccia temporanea in `updateTempo`: salti ≥3% in 1 s nello stesso brano
  43 → 34. Restano tre classi: cambi a gradino confermati che il batterista non ha fatto (61:04 122.1 → 115.2,
  76:00 122.8 → 128.1; impostati in `observe`/`observeGridStep`, fuori da `updateTempo`), scivolate del 2–3%
  a passi <1.5% dell'inseguimento VIVO, un passo isolato del ramo `slowIoi` (36:58). Gli altri salti grandi
  sono cambi di brano.
- [x] **Tenuto** `BeatDecoder::stepLacksComb`: con la parte che suona su ingresso diretto, un gradino (rilevatore
  a intervalli, `observeGridStep`, porte 4-battiti FISSO/VIVO e il voto portato) si conferma solo se il pettine
  sta più vicino al tempo nuovo che a quello tenuto. Banco (30 esecuzioni) contro la versione dell'item 79:
  >25 ms 8.92 → 8.44%, usc/min 3.48 → 3.28, oltre il 4% 138 → 105 s, oltre il 2% 742 → 716 s, tempo
  sbagliato 393 → 363 s, nessun brano peggiore; scatti 0.75 → 0.79/min. I banchi sintetici non accendono
  `sounding`. `VPTests` `--phase-lock` 20/0, `--bar` 10/0, `--new-input` 16/0, `--transport` 4/0,
  `--tempo-step` 14/0, `--tempo-slow` 13/0, `--state-timing` 0 FAIL.
- [x] Ricentro dopo 1 battito invece di 2 (salto max 0.20 e 0.12): fuori griglia 717 → 496/511 s, >25 ms
  8.92 → 8.56/8.32%, ma 5–6 brani peggiori (Sally live 3.2 → 4.8 usc/min) e colpi saltati 93 → 113.
  Non tenuto.
- [x] Ricentro morbido (spostamento distribuito su ½ o 1 battito, `TempoFollower::spreadPhase`): colpi saltati
  93 → 0/1, ma fuori griglia 717 → 1027/1057 s, >25 ms 8.92 → 9.05/8.99%, 4–6 brani peggiori. Non tenuto.
- [ ] Resta la breve pausa al ricentro (un sedicesimo saltato quando il salto in avanti ne scavalca uno):
  93 su ~2 ore di banco.
- [ ] Restano le scivolate del 2–3% in VIVO (stesso meccanismo che segue le inflessioni vere).

### 82. Il ricentro misurato in millisecondi, non in battiti 🟡 (2026-10-01, misurato — manca l'ascolto)

- [x] Log iPad su 1000 GIORNI suonato a ÷2 (~84): tempo 81.7–87.3, fase fino a +139/−126 ms, 26 ricentri in
  6 min. Rifatto con `VPTrack --octave-at 6 --octave -1`: contro lo stesso brano al livello naturale (168) la
  fase p95 sale da 36–40 a 61–68 ms. La soglia del ricentro era in battiti: 0.06 per 2 battiti è 21 ms per
  0.7 s a 168 e 43 ms per 1.4 s a 84, quindi i tempi lenti uscivano di più e più a lungo.
- [x] `BeatTracker::process`: ricentro oltre 30 ms per 1 s (uguale alla regola vecchia a 120). Banco contro
  l'item 81: >25 ms 8.44 → 8.18%, secondi oltre 30 ms dalla griglia 867 → 755, 1000 GIORNI ÷2 84 → 57 s
  (44k) e 80 → 67 s (48k); usc/min 3.28 → 3.33 (UNA CANZONE 44k 5.5 → 6.5, posizione dei colpi uguale),
  colpi saltati 94 → 104, ricentri 429 → 482. Tenuto: meno uscita sentita, al costo di qualche pausa in più.
  `VPTests` `--phase-lock` 20/0, `--bar` 10/0, `--new-input` 16/0, `--transport` 4/0, `--tempo-step` 14/0,
  `--state-timing` 0 FAIL.
- [ ] `onset_fit.py` non vale con ÷2: confronta i colpi della batteria con il sedicesimo della parte, che a
  metà tempo è l'ottavo della batteria.
- [ ] Ascolto su iPad di 1000 GIORNI con ÷2.

### 83. Tenuta del tempo sopra i ricentri: scartata di nuovo 🔴 (2026-10-02)

Richiesta: stare fermi sul tempo con inflessioni leggere, seguire solo variazioni grandi. Rifatta la tenuta
dell'item 75 (±1% attorno alla media di 30 s, cambio creduto dopo 12 s fuori banda) sopra i ricentri in ms.
- [x] H1 (con il piegamento di fase normale): oltre il 4% 91 → 43 s, tempo sbagliato 328 → 316 s, ma >25 ms
  8.18 → 8.59%, usc/min 3.33 → 3.63, ricentri 482 → 568, colpi saltati 104 → 141, 11 brani peggiori. Sui
  due live senza click invece migliora: Sally >25 ms 8.3 → 5.7%, Flamingo 3750 13.0 → 11.6%.
- [x] H2 (tempo del clock fermo, fase solo dai ricentri): scatti 0.79 → 0.07/min, oltre il 4% 20 s, tempo
  sbagliato 221 s, ma >25 ms 10.62%, usc/min 4.27, ricentri 1032, 20 brani peggiori.
- [x] Rallentando sintetico: H1/H2 restano 3–4 BPM indietro per qualche secondo. Tolte entrambe.
- [x] Verificato su materiale nuovo: 10 tratti live da 4 min (Garden Beach 15.07.26 a 600/1500/2400/3300/4200/
  5100 s, Flamingo a 1200/2400/4500/5400 s, `extract_live.swift`) e 5 brani nuovi (DEJAVU, WRECKING BALL
  versione 2, VIVERE, SING IT BACK, NonSoulFunky) a 44.1/48k. Live: >25 ms 10.00 → 9.68%, usc/min 4.25 → 4.04,
  ma scatti 0.81 → 0.92/min, oltre 30 ms dalla griglia 367 → 369 s, 2 tratti meglio e 1 peggio. Brani:
  >25 ms 6.75 → 7.11%, 3 peggio. Il guadagno su Sally non si ripete: non è una tendenza. Tolta.
- [ ] Brani nuovi con molti scatti di velocità anche nella versione attuale: DEJAVU 3.6–4.0/min,
  NonSoulFunky 4.0–4.3/min (media banco 0.8). Da guardare.

### 84. Banco veloce e ripetibile: 50 esecuzioni in ~1 minuto invece di ~1 ora ✅ (2026-10-02)

- [x] Il banco non era lento per BeatNet: la rete registrata e riletta da file (prova poi tolta) dava lo stesso
  tempo, 70 s per un brano di 2:21. Il thread di analisi dorme fino a 8 ms fra un controllo e l'altro (giusto
  su iPad, per la batteria) e `VPTrack` lo aspetta a ogni hop: il banco non poteva andare oltre ~2× il tempo
  reale.
- [x] `VP_OFFLINE_PACING` (solo probe; `NeuralBeatTracker` dorme 20 µs invece di 0.5–8 ms) e `VPTrack` che,
  con la stessa variabile, aspetta l'analisi a ogni blocco invece che a ogni hop. Lo stesso brano: 70 s → 2 s.
  Prima un frame pubblicato a metà hop cadeva su blocchi diversi da un'esecuzione all'altra (anche il banco
  lento non era identico tra due esecuzioni); ora 3 esecuzioni in serie e 6 in parallelo sono identiche bit
  per bit, e il banco intero rieseguito dà 50 su 50 file identici.
- [x] `scripts/analysis/bench_fast.py run TAG [VAR=VALORE...]` / `cmp BASE TAG...`: tutti i WAV in
  `/tmp/vp-bench/wav` (brani a 44.1/48k e i 10 tratti live) a 6 alla volta in ~45 s, confronto in ~20 s
  (colpi >25 ms, uscite/min, scatti/min, secondi oltre 30 ms dalla griglia, ricentri, colpi saltati, secondi
  oltre il 4% dal tempo del brano, brani peggiori). I numeri non sono confrontabili con i banchi lenti
  precedenti (l'analisi è sempre in pari): base nuova `fb0`.
- [x] DEJAVU e NonSoulFunky (3.6–4.7 scatti/min): ~70% degli scatti dalla piega di fase, che sotto un indizio di
  moto del decoder si apre al 7.5%. Tenerla sempre al 3% (`fbLC`): NonSoulFunky 4.4 → 3.1/min, DEJAVU 4.7 → 4.4,
  banco 1.18 → 1.10/min, ma UMBRELLA >25 ms 14.3 → 16.4% e niente altro cambia. Piega 3% + trim 1–2% +
  decoder 1–2% si sommano comunque oltre il 3%. Non tenuto.

### 85. Gli scatti vengono soprattutto dalla piega di fase; ignorare più tremolio non conviene 🔴 (2026-10-02)

- [x] Banco veloce `fb0` (50 esecuzioni): 216 scatti, 147 dalla piega di fase, 31 dal tempo del decoder,
  27 da transizioni, 11 dal trim. La piega insegue il tremolio della griglia pubblicata (~20 ms), mentre il
  pavimento di fase ignora solo 6 ms.
- [x] Solo ingresso diretto: pavimento 12 / 18 ms, via la scorciatoia `kDirectLightPhaseTau`, e 12 ms + via.
  Scatti 1.18 → 1.00 / 0.88 / 1.08 / 0.92 al minuto, ma >25 ms 8.72 → 9.25 / 9.94 / 8.80 / 9.36%, oltre 30 ms
  dalla griglia 1432 → 1584 / 1794 / 1462 / 1629 s, 9 / 20 / 2 / 13 esecuzioni peggiori. Tolte.
- [ ] Ogni prova a valle (clock) ha lo stesso scambio: velocità più ferma contro colpi meno allineati. Il
  margine vero è nella griglia pubblicata dal decoder (fase meno rumorosa, tempo meno nervoso in VIVO).
- [x] Nessun plugin o skill nei cataloghi dell'utente per beat tracking / audio DSP.
- [x] Ancora della griglia in VIVO portata gradualmente sulla retta da 24 battiti quando le due rette
  concordano entro 1% / 2%: >25 ms 8.72 → 10.74 / 10.93%, scatti 1.18 → 2.09 / 2.13, 26 / 31 esecuzioni
  peggiori. La retta lunga è ferma ma in ritardo sul battito. Tolta.

### 86. Datare il battito sull'attacco del suono invece che sul picco della rete: provato, non tenuto 🔴 (2026-10-02)

- [x] Attacco più netto in ogni hop (energia della differenza prima, blocchi di ~2.2 ms, salita contro gli 8
  blocchi prima), passato al decoder accanto alle bande; al battito accettato si sceglieva l'attacco più forte
  vicino. Misura: l'attacco sta 17–25 ms prima del battito della rete (mediana), con 11–17 ms di dispersione
  (INFINITO, VITA, I WANNA DANCE, GARDEN 1500). Spostamento fisso −20 ms, così la taratura assoluta non cambia.
- [x] Banco veloce contro `fb0` (>25 ms / usc/min / scatti/min / oltre 4% s):
  finestra 25 ms 8.81/3.57/0.96/627; 15 ms 8.75/3.67/1.15/181; soglia 1.0 8.79/3.74/1.02/199;
  35 ms + soglia 0.3 **7.83/3.43/0.87/638** (THE REASON aggancia il tempo sbagliato: 6 → 224 s);
  la stessa solo dopo l'aggancio e con la parte che suona 8.37/3.46/1.17/190 (13 brani meglio, 13 peggio);
  sull'energia normale invece della differenza 9.15 e 9.26 %. Nessuna variante migliora senza peggiorare
  molti brani. Tolto tutto.
- [ ] Il segnale c'è (la media migliora fino all'8.4% con la variante prudente), ma la scelta dell'attacco
  sbaglia spesso colpo (charleston, rullante fuori tempo). Servirebbe distinguere la cassa nel mix.

### 87. Salto di qualità: una rete «maestro» offline come verità e come insegnante 🟡 (2026-10-02, proposta — niente codice)

Valutazione delle tre strade rimaste (battiti a mano, rete migliore, cassa nel mix). Gli item 65-86 sono
quasi tutti scambi a somma zero a valle della rete (clock, ricentri, tenuta, attacchi): il margine non è
lì, e ogni giudizio poggia su misure indirette (`onset_fit`, pettine, `line_scan`), non su una verità.
- Un beat tracker **offline** allo stato dell'arte (es. *Beat This!*, CPJKU, ISMIR 2024: non causale, non
  usabile dal vivo, molto più accurato di BeatNet) unisce le prime due strade:
  - **Verità quasi gratis:** battiti e «uno» su tutto il banco e le ore di live, nel formato della Parte 2 di
    `docs/HANDOFF_LIVE_TRACKING.md`. L'utente non batte a mano: ascolta i clic sovrapposti e segnala solo
    dove sono sbagliati (ottava compresa, secondo la sua convenzione). Il banco misura allora F-measure,
    fase contro la verità, ottava giusta, e separa errore della rete da errore del decoder.
  - **Insegnante:** una rete causale nostra (stesso ingresso 272-d di `LogSpectFeatures`, stessa uscita, entra
    in `OnnxBeatModel` senza toccare il motore) addestrata sulle etichette del maestro, su materiale del
    repertorio reale (pop/dance, intro tonali, live), con in ingresso le stesse sporcizie del palco: la parte
    dell'app che rientra, stanza, livelli bassi. BeatNet non le ha mai viste.
- La cassa nel mix come DSP a sé è già smentita due volte (item 70, 86): se serve, diventa una terza uscita
  della stessa rete, non un rilevatore separato.
- [x] `scripts/analysis/truth.py`: `make` (maestro -> `WAV.truth.txt` + `WAV.clicks.wav` da ascoltare) e
  `score TAG` (pulses del banco veloce contro la verità: ottava giusta, scarto mediano, dispersione, >25 ms, uno).
  Punteggio provato su una verità finta presa dal clock stesso: 98% giusto / scarto 0; +10 ms -> +10; metà
  tempo -> «ottava».
- [x] Maestro installato dall'utente (`~/.venvs/vp-teacher`, Beat This 1.1.0, pesi `final0` scaricati con curl:
  il Python di python.org non ha i certificati). Verità sul banco veloce in 49 s; 44.1 e 48 kHz danno gli
  stessi battiti. Battiti raffinati sotto il frame con una parabola sul picco (la rete va a 50 fps).
- [x] **Il maestro è una verità credibile:** contro gli attacchi di batteria (`.onsets.npy` di `onset_fit`)
  scarto tipico 10.1 ms e 14% oltre 25 ms, contro 14.6 ms / 26% del clock dell'app; sui live 5–7 ms.
- [x] **Fase 1, decisione: il tetto NON è la rete.** 25 file (esclusi i 4 all'altra ottava, BLUE SKY e
  GARDEN 1500), scarto mediano dal mediano / quota oltre 25 ms, contro la verità:

  | | ms | >25 ms |
  |---|---:|---:|
  | batterista (ogni battito previsto dai suoi 8 precedenti: il pavimento per chi non vede il futuro) | ~7 sui live | ~5% |
  | picchi grezzi di BeatNet, quando ci sono (87% dei battiti) | 7.2 | 6.8% |
  | retta sugli ultimi 8 picchi di BeatNet, causale, picco più forte entro ±15% del periodo (4–12 battiti: 9.7–11.2) | 10.2 | 13.8% |
  | griglia pubblicata dal decoder (fase − phaseErr) | 12.1 | 17.3% |
  | clock dell'app, cioè quello che si sente (`t0`, banco veloce al codice attuale) | 16.9 | 31.6% |

  La rete basta per stare a ~10 ms; la catena perde quasi metà della precisione, e la parte più grossa fra la
  griglia del decoder e il clock (12 → 17 ms, 17 → 32%). Coerente con l'item 85: ignorare più tremolio
  peggiorava; quindi la direzione è *seguire meglio* la griglia, pagando in scatti di velocità. La retta
  semplice però si perde (ottava, buchi) dove l'app tiene: l'acquisizione e i cambi restano dell'app.
- [x] Ascolto dell'utente (2026-10-02): «sembrano giusti i clic». Quindi su VIVERE, 1000 GIORNI, DEJAVU e
  NONSOULFUNKY è l'app all'ottava sbagliata, e su EVERYTIME / SPLENDIDA / GARDEN 3300 è l'app a sbagliare l'uno.
  Aperto, non toccato qui.
- [x] **Inseguire più in fretta non serve.** Interruttori temporanei sul clock (EMA della griglia ×0.5/×0.25,
  sterzo ×0.5, piega ×2, pavimento ×0.5): dispersione contro la verità 21.6 → 21.2–21.7 ms. Tolti. Il motivo,
  battito per battito su GARDEN 3300 a 86–93 s: la band sale 115 → 120, il tempo pubblicato resta a 115 per
  ~3 s e il clock piega fino a 122 per stare dietro alla fase, −52 ms dal batterista contro −21 della griglia.
  La griglia del decoder è in ritardo su una band che si sposta.
- [x] **Tenuto: fase pubblicata dalla retta sugli ultimi 6 battiti accettati** (`BeatDecoder`, accanto a
  `gridPhaseNow` nella pubblicazione; `kPhaseLineBeats`), solo su ingresso diretto con la parte che suona,
  in FISSO/VIVO, con la retta entro l'8% del periodo e copertura > 0.6. Solo la fase, il tempo resta quello di
  prima. Causale: usa solo battiti già sentiti. Banco veloce (50 esecuzioni):

  | | disp verità ms | >25 ms verità | >25 ms batteria | usc/min | scatti/min | ricentri | saltati |
  |---|---:|---:|---:|---:|---:|---:|---:|
  | prima (`t0`) | 21.6 | 31.7% | 8.72% | 3.56 | 1.18 | 1036 | 271 |
  | retta 8 battiti | 20.2 | 26.9% | 7.41% | 3.15 | 0.68 | 1168 | 308 |
  | **retta 6 (`p6`)** | **17.9** | **26.4%** | **6.31%** | **2.91** | **0.78** | 1310 | 313 |
  | retta 4 / 5 | 17.8 / 17.8 | 26.3 / 25.7% | 5.35 / 5.84% | 2.56 / 2.75 | 0.87 / 0.76 | 1449 / 1389 | 361 / 327 |
  | retta 12 | 21.3 | 29.4% | 9.54% | 3.63 | 0.67 | 945 | 229 |
  | retta 8 + tempo della retta | 18.7 | 27.9% | 7.34% | 3.05 | 0.98 | 1077 | 263 |

  Contro la verità nessun brano all'ottava giusta peggiora di più di 2 punti (2_WRECKING_BALL +1–2); i
  «peggiorano» del banco attacchi sono brani all'ottava sbagliata o invariati sulla verità. `VPTests`
  `--bar` 10/0, `--new-input` 16/0, `--transport` 4/0, `--tempo-step` 14/0, `--tempo-slow` 13/0,
  `--state-timing` PASS.
- [ ] **Da decidere (utente): `--phase-lock` 19/1.** Il click a 156 tenuto a 78 (÷2) passa da −7.3 a −9.1 ms
  contro la linea degli 8 ms (era già al limite, item 64). Escludere il ÷2 lo fa passare (20/0) ma la
  verità torna a 20.2 ms: sotto AUTO molti brani del banco girano a ÷2, ed è lì che la retta aiuta di più
  (FLAMINGO 1200 15.9 → 10.6 ms, LET ME LOVE YOU 19.6 → 14.1). Con 12 battiti a ÷2: −8.4 ms, sempre fuori.
  La retta a ÷2 sposta la taratura di 1–3 ms anche sulla musica; causa non trovata.
- [ ] Ascolto su iPad: i ricentri salgono 1036 → 1310 sul banco (colpi saltati 271 → 313), gli scatti di
  velocità scendono 1.18 → 0.78/min.
- [ ] Il tempo pubblicato in ritardo sulla band che si sposta resta (vedi sopra): la retta lo corregge solo
  nella fase. Pubblicare anche il suo tempo migliorava la verità ma alzava gli scatti.
- [x] Ascolto dell'utente su iPad dopo la retta di fase (2026-10-02): «sembra meglio».
- [x] **L'uno: la rete lo vede, l'app lo perde.** `pDownbeat` di BeatNet piegato sui quarti veri dell'intero
  brano indica il quarto giusto in 29 file su 31 (sbaglia EVERYTIME e DEJAVU), spesso con distacco enorme
  dove l'app è a 0% (GARDEN 600 0.81 contro 0.08, UNA CANZONE 0.71 contro 0.27). Nel tempo, quarto dell'app su
  cui cade l'uno vero: ingresso spesso sul 3 per 10–20 battute (BLUE SKY, LET ME LOVE YOU, I WANNA DANCE,
  FLAMINGO 3750, GARDEN 4200); conteggio scivolato di un quarto a metà brano e mai corretto (FLAMINGO 4500
  ultime 70 battute, GARDEN 3300 66, EVERYTIME 60), perché a conteggio fidato l'item 61 chiede che rete e
  armonia concordino, e sul mix dal vivo l'armonia tace.
- [x] **Tenuto: la sola rete può spostare un quarto se il distacco è ≥ 0.30** (`kBarNetAloneMargin`,
  `BeatTracker::tryAlignFrom`). Uno giusto contro la verità 50.6 → 57.5% (0.45: 57.3), nessun brano più
  basso, fase identica. `VPTests` `--bar` 10/0, `--new-input` 16/0, `--transport` 4/0.
- [x] Simulazione al computer (battiti veri, `pDownbeat` della rete): la regola a quote dell'app 65.1%,
  verosimiglianza accumulata con oblio (log p sul quarto, log(1−p) sugli altri, 0.95 per battito, soglia 3)
  72.3%. Non portata nell'app: guadagno simile a quello già tenuto, con più rotazioni. Il tetto di un offset
  fisso per brano è 71%: anche la verità ha battute da 2 e levare, e il segnale di BeatNet battuta per
  battuta non va molto oltre. **L'uno e l'ottava sono il punto dove serve una rete migliore**: il maestro li
  azzecca (confermato a orecchio), BeatNet no. È la fase 2 di questo item, ora giustificata dalla misura.

- [x] **Fase 2 avviata: BeatNet rifinito sulle etichette del maestro** (`scripts/train_beatnet_finetune.py`).
  Dati dell'utente, tutti dalla mandata del banco, esclusi i brani del banco e i set Flamingo/Garden (esame):
  Bflat 10.01.26, Capolinea 30.05.26, 1–4.mp3, Promo Dance, ~4.4 h, 31 800 battiti, in `~/vp-train` (fuori
  da `/tmp`). Feature prese dall'app stessa (`VPActivations --features`): BeatNet in PyTorch le riproduce a
  5e-5. Etichette spostate di −46.5 ms (dove cadono i picchi di BeatNet sull'orologio dei frame), più un
  termine che tiene la rete vicina alle uscite originali (il decoder è tarato su quelle).
- [x] Incidente: il maestro su un set intero di 2 ore ha riempito la memoria e il Mac si è riavviato,
  svuotando `/tmp`. `truth.py make` ora lavora a pezzi di 5 min (scarto contro il file intero: mediana 2.4 ms,
  p90 12 ms, uni d'accordo 99%); banco ricostruito con gli stessi tagli, numeri identici (17.9 ms / 26.4% /
  uno 57.5%). Su MPS un LSTM su 340 000 frame in un colpo sbaglia: valutazione a blocchi da 10 000.
- [x] Primo modello (500 passi, KD 1.0), banco veloce con `VP_BEAT_MODEL`: livello giusto 62.2 → 71.4%,
  disp 17.9 → 16.1 ms, uno 57.5 → 64.0%, scatti 0.78 → 0.58/min; >25 ms (verità) 26.4 → 28.0%. Ottava:
  corretti 1000 GIORNI, DEJAVU, NONSOULFUNKY, VIVERE 48k, GARDEN 600; rotti EVERYTIME (123 → 61),
  2_WRECKING_BALL (115 → 57), UNA CANZONE (85 → 171), GARDEN 2400. Saldo positivo ma non robusto: servono
  più dati e più vari.
- [x] Varianti (passi / peso KD), banco veloce contro la verità (livello giusto %, disp ms, >25 ms %, uno %,
  scatti/min): oggi 62.2 / 17.9 / 26.4 / 57.5 / 0.78; 500/1 71.4 / 16.1 / 28.0 / 64.0 / 0.58; 2000/1 68.2 /
  21.5 / 25.6 / 68.2 / 0.74; 2000/0.3 69.5 / 20.4 / 28.2 / 67.9 / 0.66; 500/3 73.4 / 14.9 / 25.8 / 69.8 / 0.51;
  **1000/3 71.9 / 13.0 / 22.2 / 67.3 / 0.53**; 500/6 70.1 / 15.5 / 25.0 / 66.3 / 0.62. Sulla coda tenuta fuori
  delle serate di addestramento vince 2000/0.3 (F 0.938, uno 0.848) ma sul banco perde: più addestramento
  impara quelle due serate, il KD alto generalizza meglio.
- [x] **Tenuto 1000/3 come `Assets/Models/beatnet.onnx`** (`train_beatnet_finetune.py s1kk3 --steps 1000
  --kd 3.0`; l'originale GTZAN resta in git). `VPTests` con il modello nuovo: `--phase-lock` 20/0 (il 156 ora
  si legge 156: era 19/1), `--bar` 10/0, `--new-input` 16/0, `--transport` 4/0, `--tempo-step` 14/0,
  `--tempo-slow` 13/0, `--state-timing` 0 FAIL. Banco incorporato identico a `VP_BEAT_MODEL`.
- [x] **Contaminazione trovata e tolta:** controllo per inviluppo d'attacco (correlazione normalizzata, due
  sonde da 60 s di ogni brano del banco contro ogni altro file) — Promo Dance contiene audio di NonSoulFunky
  del banco (0.71; il resto ≤ 0.4, il caso casuale 0.06). Il modello 1000/3 l'aveva sentito: la sua
  correzione d'ottava su NONSOULFUNKY non vale. Promo Dance spostato in `~/vp-train/excluded`.
- [x] Dati nuovi dell'utente (2026-10-02): 13 brani in studio (38, 12, 18, 30, 33, 40, 41, Back to black,
  Snow on the sahara, Il mio giorno migliore, Il mare impetuoso, Try, Dance Mesh — quest'ultimo controllato:
  non è NonSoulFunky, 0.15) e tre serate (nsf08 11 2008, NSF FBI ottobre 2008, SONG00). Totale 9.56 h, 63 000
  battiti. `--balance 0.5` sceglie i file per radice della durata, così lo studio non sparisce sotto i live.
- [x] **Ripetizioni con seme diverso** (banco senza NONSOULFUNKY, 48 file; livello giusto % / disp ms / >25 ms % /
  uno % / file all'ottava giusta): BeatNet 68.6 / 20.4 / 30.2 / 69.0 / 38; s1kk3 70.6 / 13.7 / 23.3 / 74.4 / 39;
  stessa ricetta senza Promo Dance, seme 0 e 1: 75.7 / 15.9 / 28.0 / 75.5 / 42 e 80.2 / 15.2 / 26.5 / 76.4 / 45;
  9.56 h 1000 passi 70.2 / 18.3 / 26.3 / 74.8 / 39; 2000 passi seme 0 e 1: 76.0 / 14.8 / 26.4 / 69.8 / 43 e
  62.3 / 14.3 / 23.9 / 71.6 / 36; bilanciato 74.3 / 15.8 / 28.4 / 71.7 / 42. Media dei pesi (`scripts/
  average_onnx_models.py`) di 6 e di 4 modelli: 72.7 / 16.2 / 29.7 / 73.3 / 41 e 75.8 / 15.9 / 28.9 / 71.9 / 42.
  **Solido:** ogni modello rifinito migliora la fase (20.4 → 13.7–18.3 ms) e l'uno (69 → 70–76%). **Non
  solido:** l'ottava oscilla col seme più che con i dati (62–80%), e 9.56 h non battono chiaramente 4 h.
- [ ] **L'ottava è una decisione sul filo nel decoder, non solo un limite della rete:** con la stessa rete lo
  stesso brano a 44.1 e 48 kHz finisce spesso su ottave opposte (VIVERE, 1000 GIORNI, EVERYTIME, UNA CANZONE si
  ribaltano fra modelli e fra frequenze). Prossima leva: rendere stabile la scelta d'ottava in acquisizione
  (stessa scelta per piccole perturbazioni), misurata con le coppie 44.1/48 come repliche.
- [x] **Modello dell'app cambiato in o1k3s1** (4 h senza Promo Dance, 1000 passi, KD 3, seme 1): s1kk3 metteva
  SALLY live a metà tempo (52 contro 104). o1k3s1 contro BeatNet originale: livello giusto 68.6 → 80.2%,
  disp 20.4 → 15.2 ms, >25 ms 30.2 → 26.5%, uno 69.0 → 76.4%; corregge VIVERE, WRECKING BALL 04, 1000 GIORNI
  44k, DEJAVU, rompe solo EVERYTIME 48k. `VPTests` `--phase-lock` 20/0, `--bar` 10/0, `--new-input` 16/0,
  `--transport` 4/0, `--tempo-step` 14/0, `--tempo-slow` 13/0, `--state-timing` 0 FAIL. Scelto anche per il
  seme: va riprovato quando la scelta d'ottava sarà stabile.
- [x] Ascolto su iPad con o1k3s1 (2026-10-02): «sembrano tutti abbastanza corretti sulle ottave»; il tempo non
  sembra «davanti» al batterista (l'anticipo di ~5 ms contro la verità non si sente).
- [ ] Ottava ancora fragile: 1000/3 contro oggi corregge VIVERE 48k, WRECKING 04, 1000 GIORNI, DEJAVU,
  NONSOULFUNKY, GARDEN 2400/600 e ne rompe EVERYTIME, UNA CANZONE 44k, ASPETTANDO 48k; lo stesso brano a
  44.1 e 48 kHz può finire su ottave diverse. Servono più dati, e più vari (studio, altri palchi).
- [x] Taratura assoluta: «anticipo» contro la verità 26 → 31 ms (il click `--phase-lock` resta entro 8 ms); all'ascolto non si sente.
- [x] Ascolto su iPad (vedi sopra). Licenze: pesi derivati da BeatNet (CC BY 4.0, `docs/LICENSES.md`); addestrati anche su
  1–4.mp3 e Promo Dance, di cui l'utente deve confermare i diritti prima di distribuire l'app.

- [x] **Ingresso a tempo sbagliato, misurato** (tempo vero = mediana degli intervalli del maestro nei 4 s dopo
  l'entrata, ottava ripiegata; 46 file senza NONSOULFUNKY): con o1k3s1 la parte entra sbagliata di oltre il 3%
  in 21 file su 46 (BeatNet originale: 21), errore medio 10.9%; fase nei primi 8 s mediana 79 ms. Esempi: SALLY
  live 103 BPM, entra a 6 s a 80 e ci resta fino a ~20 s; ASPETTANDO 88, entra a 81. L'utente sente
  ASPETTANDO «per un secondo al doppio, poi subito alla metà» (sul banco non riprodotto: entra a 81).
  Rapporti tipici 4:3 e 3:2, o il tempo dell'intro.
- [x] Esperimento (tolto, `BeatTracker::updateState`): entrare solo se il tempo del decoder e il pettine
  (`hyp.combBpm`) concordano entro il 3% (o ×2/÷2). Banco veloce: entrate oltre il 3% 21 → 10, errore medio
  10.9 → 4.7%, fase primi 8 s 79 → 51 ms; SALLY entra a 12.5 s a 102 invece che a 6 s a 80; ASPETTANDO 48k
  invariato (il pettine dice 81 anche lui). Costo: ingresso in mediana 3.0 s più tardi (live 3.6 s, studio
  0), massimo 18 s. Con anche `levelSettled`: errore 2.5%, 8 file, ma 7.4 s di ritardo. Ottava sul brano
  intero 45 → 43 file (di nuovo la scelta sul filo). **Da decidere con l'utente: ingresso più tardi ma giusto.**
- [x] **Tenuto (l'utente: «ok che entri 3–4 s più tardi ma al tempo giusto»):** `BeatTracker::entryTempoAgrees`
  (`kEntryCombAgree` 3%, ×2/÷2 ammessi), esente con TAP, tempo dell'utente, inseguimento spento e armonia
  (senza pettine: `--harmonic-entry` non entrava più). I «3–4 s» erano la mediana: il controllo non ritarda i
  brani chiari e aspetta solo dove il tempo è ambiguo, ed è lì che corregge (ASPETTANDO 3.3 s, INFINITO 4.9,
  SALLY 6.5, GARDEN 600 5.3, FLAMINGO 5400 5.9, DEJAVU/1000 GIORNI ~11, VIVERE/FEEL 17–18). Tetto
  `kEntryCombWaitSec`: 4 s 21/46 storti (nessun beneficio), 8 s 17/46, **12 s 12/46 (tenuto, mediana 3.0 s,
  max 10.9)**, senza tetto 10/46 (max 18). `VPTests --level`: «ingresso entro due battute» → «due battute +
  4 s» (sintetico 91 BPM 2.6 → 7.6 s, 168 0.8 → 4.3 s); restano le 3 bocciature di prima (clip). `--state-
  timing`: i test chiamano `updateState` senza pettine, ora dichiarano l'accordo e verificano che senza non si
  entri. Da riascoltare su iPad.
- [x] Banco: aggiunti **08 UN ORA SOLA** (76.6 BPM, l'app suona 153 tutto il brano: i «colpi doppi» che
  l'utente sente nei brani lenti) e **11 SEE YOU AGAIN** (81, ottava giusta, fase 23 ms). Controllati per
  inviluppo: non sono nei dati di addestramento (≤ 0.16).
- [x] Ottava dalla battuta di 4: l'autocorrelazione di `pDownbeat` a 4 battiti del livello candidato (meno metà
  di quella a 2) sceglie il livello del maestro in 49/52 file **sul brano intero**, compresi tutti gli errori
  rimasti (UN ORA SOLA, UNA CANZONE 48k, FLAMINGO 1200, EVERYTIME 48k; sbaglia VIVERE e GARDEN 1500). Ma
  causale nei primi 16–20 s dall'inizio della musica: giusta 36/51, incerta 7, sbagliata 8 — non abbastanza
  prima dell'ingresso, e l'ottava non cambia sotto una parte che suona. Non usato.
- [x] Tenuta nei brani lenti (la lamentela dell'utente): il tempo segue come negli altri brani (scarto mediano
  0.7%), è la **fase** che peggiora: 22–23 ms mediana (p90 56–147) contro 10–12 nei brani veloci. 1000 GIORNI:
  picchi rete 6.6 ms, griglia 16.7, clock 22.2 (in VIVO il 76% del tempo); SEE YOU AGAIN e UN ORA SOLA: già i
  picchi sono sparsi (12–16 ms) perché la rete segna anche gli ottavi (1.6–2.2 picchi per battito vero).
- [x] `--kdoff W` in `train_beatnet_finetune.py`: KD solo entro 3 frame dai battiti del maestro. 1000/3/0.3
  seme 0 (`m1k3o03s0`) contro l'app attuale: disp 17.1 → 14.8 ms, >25 ms 28.9 → 26.3%, livello 77.7 → 77.1%,
  uno 76.5 → 74.8%; SEE YOU AGAIN 22.9 → 19.1 ms, 1000 GIORNI 21.9 → 19.7, corregge UNA CANZONE 48k e FLAMINGO
  1200; seme 1 più debole (livello 73.1%). UN ORA SOLA resta a 153: i levare scendono solo a 0.51 (battito 0.78).
- [x] `m1k3o03s0` messo come `Assets/Models/beatnet.onnx` per l'ascolto (incorporato = `VP_BEAT_MODEL`, banco
  identico). `VPTests` `--phase-lock`, `--bar`, `--new-input`, `--transport`, `--tempo-step`, `--tempo-slow`,
  `--state-timing`, `--harmonic-entry` senza errori; `--level` 11/5: oltre alle 3 di prima, **168 BPM a −12 dB
  finisce a 84** (con o1k3s1 restava 168); in cambio 52 BPM a 0/−6 dB ora legge 51.4 invece di 104 (non
  giudicato). Il brano veloce vero del banco (GARDEN 2400, 159) è invariato. Se l'ascolto non lo giustifica, si
  torna a o1k3s1: `cp ~/vp-train/models/o1k3s1.onnx Assets/Models/beatnet.onnx`.
- [x] Ascolto utente (2026-10-02): 1000 GIORNI a 84 giusto, segue abbastanza bene, ma all'inizio oscilla e nel
  brano si sentono **piccoli «crack» ai riallineamenti**; colpi «leggerissimamente spostati, non so se in
  anticipo o ritardo». Vincolo ribadito: non peggiorare, correzioni globali.
- [x] **Crack = ricentri a scatto.** 1000 GIORNI: 35 in 281 s, 31–179 ms ciascuno (`snapPhase` mette subito un
  colpo nella nuova fase: un sedicesimo saltato o ripetuto). **Tenuto:** `TempoFollower::glidePhase` (lo
  spostamento speso in ¼ di battito come piega della velocità, mai sotto metà velocità: ogni sedicesimo suona
  una volta) e ricentro più piccolo e precoce (15 ms per 0.5 s invece di 30 ms per 1 s). Banco contro la
  verità: disp 14.8 → 11.8 ms, >25 ms 26.3 → 18.7%; contro la batteria 6.20 → 4.05%, uscite 3.16 → 2.10/min,
  scatti 0.58 → 0.33/min, salti 1158 → 21, sedicesimi saltati 254 → 7; **42 file meglio, 0 peggio**, nessuna
  ottava cambiata. 10 ms: media un po' meglio ma 2 file peggio; attesa 0.3 s: peggiora la griglia stessa.
  `VPTests` come prima (`--level` 11/5, le stesse 5). 1000 GIORNI: salti 35 → 0, errore mediano 20 → 14 ms.
- [x] «Leggermente spostati»: non è uno spostamento fisso (la batteria cade in media 4.7 ms dopo il clock, ±3 ms
  fra i brani; la latenza d'uscita non è compensata per i brani caricati, giusto) ma la dispersione colpo per
  colpo — quella che il passo sopra riduce.
- [ ] **Inizio che oscilla:** in 25 file su 54 almeno uno dei primi 4 colpi dopo l'ingresso è oltre 80 ms dal
  battito vero. Due tipi: ingresso sul levare (THE REASON, SALLY: ~−310 ms costanti; la griglia del decoder
  resta sul levare da 4 a 16 s e `checkGridPhase` la gira solo a 17 s) e fase che salta fra battere e levare
  (EVERYTIME, DEJAVU, 1000 GIORNI). Provato e tolto: non entrare finché la piega (`checkGridPhase`) dice
  «levare» — nessun effetto (25/54), la piega non ha abbastanza dati prima dell'ingresso.
- [ ] UN ORA SOLA a 153: serve un'idea nuova per i brani lenti con ottavi forti (la battuta di 4 funziona sul
  brano intero ma non prima dell'ingresso).

### 88. Dal vivo: START premuto, band che suona, percussioni mute 🟡 (2026-10-05, misurato — da riprovare dal vivo)

Segnalazione dell'utente dopo un live: ogni tanto, pur con START premuto e la musica nell'impianto, le
percussioni non si sentono; cambiare 48/44.1/auto non serve; chiudere e riaprire l'app a volte sì, poi di nuovo
no. L'ingresso arriva basso se il mixer non è al massimo, e alza «mic input» al massimo.
- [x] **Riprodotto:** il banco non poteva vederlo perché ogni tratto live comincia con 3 s di silenzio (che dà
  sempre il passaggio silenzio→musica, `sawInputStart`). Stessi tratti senza silenzio, ingresso diretto
  (`VPTrack` senza `--player`): GARDEN 3300 non entra **mai** a nessun livello; FLAMINGO 4500 entra a 62 s a
  0 dB e mai da −12 dB in giù. Il tempo è agganciato (118 BPM, confidenza 1.0, FOLLOWING), ma l'altra via
  (`alreadyPlaying = heardMusic`) chiede livello grezzo > 0.040 **prima** del guadagno d'ingresso (alzare
  «mic input» non conta) **e** `rhythmSeen`: quota di bassa frequenza > 0.30 per 0.33 s — la mandata dal vivo
  ne ha 0.13–0.24. Riaprire l'app funziona solo se poi arriva una pausa fra due brani (nuova epoca).
- [x] **Tenuto:** `BeatTracker` `lineLockSamples` — su ingresso diretto (mai il microfono dell'iPad, dove una
  stanza vuota aggancia davvero), con START, un aggancio FOLLOWING con confidenza > 0.80 e pettine d'accordo
  tenuto 4 s vale come «qualcuno suona» (`kLineLockSec`, `kLineLockConf`). Senza silenzio iniziale ora entra a
  ~14 s da 0 a −30 dB su entrambi i tratti; solo fruscio (0.003 e 0.05) resta muto. Banco: cambiano solo i brani
  con intro lunghe — UMBRELLA entra a 38 s invece di 120 (tempo giusto, fase 25 ms invece di 53), NONSOULFUNKY
  26 invece di 74, 2_WRECKING_BALL 14.5 invece di 51 (sull'intro, coerente); medie contro la verità livello 77.9
  → 78.3%, uno 68.7 → 69.7%, disp 11.8 invariata. `VPTests` invariati (`--level` 11/5 come prima).
- [x] **Interruttore START SUBITO** (richiesta dell'utente, SETUP › TEMPO, accanto a SEGUI/FISSO; **spento di
  default**, salvato nelle preferenze `startImmediately`): acceso, START salta le prove che qualcuno suona
  (`sawInputStart`, `heardMusic`, aggancio di 4 s) e l'attesa del pettine (`entryTempoAgrees`); la parte entra
  appena l'analisi ha una griglia. `EngineSettings::startImmediately` → `BeatTracker::setStartImmediately`.
  `VPTrack --start-now`: GARDEN 3300 senza silenzio a −24 dB entra a 1.6 s invece di 13.7; solo fruscio resta
  muto in entrambi i casi (nessuna griglia). Default invariato; `VPTests` invariati; app macOS compilata —
  la disposizione dei tre pulsanti nella scheda TEMPO non è ancora vista a schermo.
- [x] **START SUBITO suona anche senza segnale** (richiesta dell'utente, 2026-10-05): acceso, la parte suona da
  START qualunque cosa abbia trovato l'analisi, sul tempo del clock (ultimo, TAP, o 120). L'uscita «suona»
  (`percussionShouldPlay`) è ora separata da `sounding` («suona su una griglia trovata»), che continua a
  regolare tenuta d'ottava, keep del decoder e ricentri, `hadPlayed`. Alla cieca il clock prende un tempo solo
  quando il tracker esce da LISTENING e con battito regolare e confidenza > 0.28 (`blindUnsure`): sul silenzio
  digitale il decoder pubblica comunque 137.9 BPM a confidenza 0.02 dopo 15 s, e un solo blocco sopra soglia
  bastava a spostarlo. `VPTrack --start-now`: silenzio, fruscio basso e alto suonano al 100% fermi a 120;
  GARDEN 3300 senza silenzio va subito a 118. Su 8 brani del banco, dopo l'aggancio livello/disp/>25 ms
  uguali entro rumore; l'uno cambia (THE REASON 81 → 95, INFINITO 87 → 99, UMBRELLA 100 → 75, 1000 GIORNI
  81 → 72) perché non c'è più l'ingresso quantizzato sul battere. Spento: banco 54/54 identico, test invariati.
- [ ] Riprova dal vivo, anche con il mixer non al massimo; se si ripete, mandare
  `Documents/VirtualPercussionist-stopstart.log`.
- [ ] Con l'ingresso a −12 dB su FLAMINGO 4500 compaiono due pause in più della parte (65–70 s, 73–80 s) che a
  0 dB non ci sono; non viene da `BandDynamics` (relativo al brano). Da guardare.

### 89. Pulizia del codice inutilizzato ✅ (2026-10-05, verificata bit per bit)

Metodo: `clang -fsyntax-only` con tutti gli avvisi «unused» su ogni file di `Source/`
(`build-host/compile_commands.json`, ora esportato) più un incrocio dei nomi dichiarati negli header contro
tutto `Source/`, `Tests/`, `scripts/` (compresi i file solo iOS), escludendo i nomi che esistono in JUCE.
- [x] Tolti: costanti `kPriorCentreBpm` (doppione di `BeatHmm::priorCentre`) e `kBusyBins`; catture `this`
  inutili; funzioni mai chiamate `offbeatRatio`, `lastClarity`, `cancelLatencyMeasurement`,
  `recordedLoopPlaying`, `currentStyle`, `barsObserved`/`kickBins`/`bodyBins`/`highBins`, `chromaNow`/
  `changeNow`, `glideActive`, `pulsesFor` (la skill percussioni lo citava: il clock emette sempre
  `kClockPulsesPerBeat` = 4), `layoutTransport`; campi scritti e mai letti `beatsHeld`, `downbeatStrength`
  (+ `lastDownbeatStrength`), `excludedPoint`, `beatsLocked` (+ `lastBeats`), `analysisWakeups`,
  `preparedInputs`, `kickScratch` (allocato e mai usato); nel motore il percorso «loop di percussioni»
  mai collegato (`loadPercussionLoop`/`clearPercussionLoop`, membri `stretch`/`stretcher` e il ramo
  `stretcher.hasLoop()` controllato a ogni blocco). `TimeStretchEngine`/`StretchFactor` restano: hanno test.
- [x] Lasciati apposta: i tre campi di `MainComponent` segnalati inutilizzati (sono usati nel codice solo iOS);
  la cattura `kDamp` (altri compilatori possono richiederla); i getter citati come concetto in docs/skill
  (`swingTolerance`, `kickIsTrusted`, `attackLeadSamples`); le manopole dei loop registrati in standby
  (`setStretchLimit`, `setSwingTolerance`, `setAccentLayer`, `loadLoopBank`, `isArmed`, `lastMiss`…);
  `useNnapiOnAndroid` (Android più avanti).
- [x] Verifica: compilano tutti i bersagli (app, `VPTests`, 15 sonde); banco veloce **54/54 file identici bit
  per bit** a prima; `VPTests` `--phase-lock` 20/0, `--bar` 10/0, `--new-input` 16/0, `--transport` 4/0,
  `--tempo-step` 14/0, `--tempo-slow` 13/0, `--rhythm` 3/0, `--loops` 58/0, `--percussion` 17/0,
  `--state-timing`/`--harmonic-entry` senza errori; `--level` 11/5 e `--leak` 46/3 identici alla versione
  committata (le stesse bocciature già presenti; le 12 righe che differiscono in `--leak` differiscono anche
  fra due esecuzioni dello stesso codice).

### 90. Dal vivo: livello d'analisi sul MIXER, levare correggibile a mano, volume del clap 🟡 (2026-10-05, misurato — da provare dal vivo)

Richiesta: «un salto di qualità per il live (opzione mixer)», provabile solo con BRANO. Nel frattempo due segnalazioni:
«a volte il tempo viene agganciato in levare e non riesco nemmeno a correggere» e «il clap è molto più basso
delle altre percussioni».
- [x] **Banco veloce sul percorso MIXER** (`bench_fast.py run TAG ARGS="--gain -12"`: `VPTrack` senza `--player`,
  cioè `kitMic`, stesso audio). A 0 dB è uguale a BRANO; con la mandata più bassa no. Contro la verità del maestro
  (disp / p90 / >25 ms / levare): 0 dB 11.9 / 35 / 18.7% / 0.65%, −12 dB 14.5 / 56 / 20.6% / 1.28%, −24 dB 14.6 /
  52 / 20.6% / 1.13%. Motivo: le feature di BeatNet sono log10(1 + modulo), quindi il livello è un ingresso. Il
  guadagno d'analisi alza solo fino a 0.20; un file passa al suo livello (inviluppo 0.14–0.55 sul banco, mediana
  0.27; 0.43 sulle mandate dei vostri live, lo stesso livello delle feature su cui è stata rifinita la rete). Dal
  mixer non al massimo la rete lavorava ~7 dB più in basso.
- [x] Bersagli provati (ottava giusta % / disp / p90 / >25 ms a 0, −12, −24 dB): 0.20 (oggi) 77.9/11.9/35/18.7,
  77.3/14.5/56/20.6, 82.9/14.6/52/20.6; 0.30 73.9/11.0/38/17.7, 73.9/12.7/40/18.4, 80.1/12.2/37/19.2; **0.40**
  74.2/11.4/34/18.4, 76.7/11.0/31/16.8, 81.4/12.0/38/18.5; 0.50 75.9/12.1/47/19.6, 75.7/11.3/38/17.3,
  83.1/12.2/42/19.0. Simmetrico a 0.30 identico a 0.30 (gli inviluppi dei file quasi mai sopra 0.30). 0.40 fisso
  rompe anche `VPTests --level` (168 BPM a −6 dB legge la metà): l'ottava si decide su un altro segnale.
- [x] **Tenuto: 0.40 solo su MIXER e solo con la parte udibile** (`kMakeupPlayingPeak`, `processBlock`); in aggancio
  resta 0.20, così l'ottava si sceglie come prima. MIXER: 0 dB 76.8 / 11.6 / 36 / 18.6% / levare 0.78%; **−12 dB
  76.7 / 11.3 / 32 / 17.5% / 0.33%; −24 dB 82.5 / 12.3 / 39 / 18.9% / 0.50%**. Sullo stesso codice anche in BRANO
  la fase era uguale e l'ottava −1.0 (UNA CANZONE 48k va al doppio dopo l'ingresso): per questo BRANO resta escluso,
  e il banco BRANO è identico a prima. `VPTests` `--level` 11/5 (le stesse 5), `--new-input` 16/0, `--transport`
  4/0, `--state-timing` senza errori, `--phase-lock` 20/0.
- [x] **Levare: perché non si correggeva.** Con la parte che suona nessun automatismo può spostare la griglia di mezzo
  battito (`checkGridPhase` spento, il keep difende `lastBeat` dai charleston); «L'1 è QUI» rinominava solo il
  conteggio e il primo TAP spostava il clock, ma il decoder lo riportava indietro in meno di un secondo. Sul banco
  BRANO 0.61% dei battiti in levare, in tratti fino a 17 s (FLAMINGO 4500 66–83 s, THE REASON 7–16 s, SALLY 12–18 s);
  MIXER −12 dB fino a 40 s (ASPETTANDO 181–220 s).
- [x] **Tenuto: «L'1 è QUI» premuto a metà del battito sposta la griglia di mezzo battito** (`kLevarePressBand` 0.15,
  cioè fase 0.35–0.65). Il clock scivola su un battito (`glidePhase`: mezza o 1.5× velocità, nessun colpo saltato o
  doppio), il decoder sposta l'àncora di mezzo periodo, butta i battiti presi in levare e tiene il tempo per 8 battiti.
  Fuori da quella fascia il tasto fa quello che faceva. `VPTrack --declare-at T` premendo sul battito vero: FLAMINGO
  4500 35 → 0 battiti in levare, THE REASON 14 → 0, EVERYTIME (MIXER −12) 36 → 0, SALLY 15 → 4 (resta un altro
  episodio a 262 s). Una pressione sbagliata su una griglia giusta si annulla premendo di nuovo sull'1 (VITA). Riaprire
  `checkGridPhase` con la parte che suona, ri-misurato: peggio (disp 11.8 → 13.4 ms). `VPTests --bar` 13/0 con due
  controlli nuovi (clock e decoder).
- [x] **Clap:** misurato ogni strumento da solo a fader pieno (K-pesato, 50 ms più forti di ogni colpo, dance/pop/rock/
  samba a 120, `VPRender --raw`): shaker 0, cembalo −0.5, conga +2.0, clap −2.5 dB, e sotto la band il clap cade sul
  rullante. `kClapLevel` +4.5 dB, legato al suono del clap e non alla manopola: ora +2.0 come le conga.
- [x] **Allineati anche shaker e cembalo** (richiesta dell'utente): `kShakerLevel` +2.0 dB, `kCembaloLevel` +2.5 dB, legati
  al suono come il clap. Rimisurato, rispetto allo shaker in media: cembalo +0.0, conga −0.0, clap −0.1 dB (fra gli stili
  ±1.4 dB, dipende dal pattern). Tutti e quattro insieme (dance, riverbero 0.3): 0.39% dei campioni sopra il ginocchio del
  soft clip. Banco BRANO identico bit per bit (lo shaker suona nel banco), MIXER −12 dB stessi punteggi. `VPTests
  --percussion` 17/0.
- [ ] Prova dal vivo con il MIXER, anche con la mandata non al massimo: la fase dovrebbe tenere come in BRANO.
- [ ] Ascolto: i quattro volumi allineati? «L'1 è QUI» sul levare: va premuto sull'1 (premendo su un altro quarto la griglia si
  sistema ma il conteggio prende quel quarto come 1).
- [ ] Il levare automatico resta: il tasto è la via d'uscita, non la cura.

### 91. Controllo live e BRANO: latenza, pause a metà brano, cambi di brano, deriva 🟡 (2026-10-05, misurato — da provare dal vivo)

Richiesta: «verifica se ci possono essere altri problemi, principalmente live (latenza o altro), e anche in BRANO»;
poi «sembra che dopo un po', su BRANO, tendano a suonare indietro».
- [x] **Deriva in BRANO: non c'è nel motore.** Scarto con segno contro la verità per finestre di 30 s: pendenza mediana
  +0.36 ms/min sul banco (NONSOULFUNKY +20…+36 ms per 8 minuti); stessa canzone ripetuta 4 volte, 32 minuti di
  sessione senza riavvii: −0.05 ms/min. Analisi tenuta indietro (`VPTrack --lag`) di 0.3 / 1 s: anticipo mediano
  invariato (+24…+27 ms), nessuna deriva, solo meno precisione (disp 11.8 → 12.9 / 15.6 ms). In BRANO il file va
  all'analisi e all'uscita con gli stessi campioni, quindi nessuna latenza relativa; il ricampionatore verso
  l'analisi è esatto (double) e i frame sono datati su contatori a 64 bit.
- [x] **Ipotesi per l'iPad:** il thread di analisi era un `std::thread` con QoS di default, che iOS può far scivolare
  dietro l'interfaccia e un processore caldo; più resta indietro, più lunga la proiezione e più un piccolo errore di
  tempo diventa ritardo. Ora `QOS_CLASS_USER_INTERACTIVE` all'avvio del worker (solo Apple). **Non misurato sull'iPad:**
  se succede ancora, guardare `lead` nel pannello DEBUG (o `VPLAG` nella console di Xcode): se cresce, è questo.
- [x] **Latenza misurata ferma:** il tasto LATENZA salvava la misura per sempre e la usava al posto di quella riportata;
  dopo un cambio di buffer (256 → 512 = +10.7 ms a 48 kHz) o di CLOCK la parte arrivava in ritardo di un buffer
  senza segnali. Ora `roundTripMs()` = misura + (riportata ora − riportata al momento della misura), base salvata
  nelle preferenze (`measuredLatencyBaseMs`); una misura vecchia senza base si usa com'era. `VPTests --transport` 5/0.
- [x] **Canceller del MIXER sulla latenza sbagliata:** sottraeva il ritorno della nostra parte al ritardo riportato dal
  sistema, non a quello misurato, che è esattamente quel percorso (uscita → mixer → ingresso). Ora usa `roundTripMs()`.
  Conta solo se nella mandata torna anche l'iPad.
- [x] **Pausa a metà brano sul MIXER:** una ripartenza dell'analisi (salto di livello dopo un tratto piano) era sempre a
  freddo sul MIXER; su GARDEN 2400 (vostro live, la band non si ferma) la parte che suonava andava 107 → 138 → 143 → 85
  → 54 e restava a metà tempo. Ora, se la parte suonava su un livello confermato e la confidenza era > 0.5 al salto, la
  ripartenza tiene pettine e modello come in BRANO (item 54): resta 105–111, ottava giusta su quel brano 54 → 74%.
  Banco MIXER: ottava 76.8 → 77.3% (0 dB), 76.7 → 77.3% (−12 dB), il resto uguale; BRANO identico. Cambi di brano veri
  dopo 8 s di pausa (sei coppie di vostri live): confidenza 0 al salto, invariati.
- [x] **Cambio di brano dal vivo con pausa corta** (risolto con lo STOP, item 92): con 2–4 s fra due brani il guardiano del livello non scatta (vuole
  ~4 s di quasi silenzio), la griglia vecchia resta difesa (il rifiuto dopo un buco con la parte che suona) e la parte
  rientra nel brano nuovo dopo 13–20 s (stesso brano partito da solo: ~5.6 s; dopo 8 s di pausa: 0.3–4 s). Niente tempo
  sbagliato, ma ingresso tardi; con applausi/voci nella mandata la pausa non è mai «silenzio». Un «nuovo brano»
  esplicito all'attacco (`notifyInputRestart`, come caricare un file) lo porta a 1.6–2.0 s. Da decidere con l'utente
  se un comando manuale o un riconoscimento automatico (più rischioso: a metà brano lo stesso segnale è un reticolo
  avanzato, fixture D).
- [x] Ingresso che satura: `VPTests --level` «clip» (+12 dB oltre il pieno) già bocciato prima; c'è l'indicatore
  d'ingresso con la fascia buona −12…−1 dBFS.
- [ ] Dal vivo: misurare la LATENZA al soundcheck con il ritorno del mixer collegato.

### 92. Dal vivo: STOP tenuto fra due brani = nuovo brano 🟡 (2026-10-05, misurato — da provare dal vivo)

Proposta dell'utente: invece di un comando «nuovo brano» o di un riconoscimento automatico, farlo con STOP.
- [x] **Tenuto:** su ingresso dal vivo (MIXER e microfono; BRANO no, lì ogni file riparte già), se lo STOP dura almeno
  3 s (`kNewSongStopSec`) **e** durante lo STOP l'analisi è rimasta più di 2 s senza accettare battiti
  (`kSongEndGapSec`, `BeatTracker::secondsSinceBeat`), l'analisi riparte da zero una volta, come per un file nuovo
  (`notifyInputRestart`). Succede mentre si è ancora in STOP, così il brano nuovo viene già agganciato quando si preme
  START. Uno STOP/START rapido (il gesto di riallineamento) resta com'era.
- [x] Cambi di brano (`scripts/analysis/stop_song.py change`: sei coppie dei vostri live, pause 2–12 s, STOP alla fine
  di A): START mezzo secondo dopo l'attacco di B, ingresso giusto mediano **11.3 → 1.9 s**; 2 s dopo **11.9 → 0.9 s**.
  START un secondo prima dell'attacco: se lo STOP è sotto i 3 s non cambia niente; con pause di 8–12 s in silenzio
  (dove anche prima ripartiva da sola) simile, un brano più lento (FLAMINGO 1200 → GARDEN 5100 3.3–5.0 → 10.1 s).
  Tempo suonato sbagliato in totale 192 → 167 s.
- [x] Senza la condizione sui battiti uno STOP lungo **a metà brano** (la band continua) costava: STOP 4 s ingresso
  mediano 0.4 → 1.5 s, 4 brani su 23 fino a 16 s a tempo sbagliato. Con la condizione (`stop_song.py inside`): STOP 4 s
  mediana 0.4 s, sbagliata 50 → 46 s; STOP 8 s p90 3.0 → 0.7 s, sbagliata 56 → 53 s; nessun brano peggiore.
- [x] `VPTests --transport` 6/0 (nuovo: STOP 3.5 s e 8 s senza battiti su MIXER ripartono una volta, 2 s no, BRANO no);
  `--bar` 13/0, `--new-input` 16/0, `--state-timing`, `--percussion` 17/0.
- [ ] Dal vivo: a fine brano premere STOP e tenerlo almeno 3 s; START quando parte il brano nuovo (anche un attimo dopo
  l'attacco va bene). Se lo STOP/START serve solo a riallineare a metà brano, va fatto rapido come prima.

### 93. Percussioni sempre leggermente in ritardo: la trattenuta d'attacco da 12 a 6 ms 🟡 (2026-10-05, misurato — da ascoltare)

Segnalazione: «sembra ancora che le percussioni suonino sempre leggerissimamente in ritardo, di pochissimi millisecondi».
- [x] Causa: `kHeardEarlyHoldSec` (12 ms) trattiene ogni colpo dopo la compensazione dell'attacco. Fu tarata il 22/09
  guardando solo i 120 BPM (residuo del click +9.1 ms). Da allora il clock è meno in anticipo: il residuo di
  `VPTests --phase-lock` a 78/100/120/138/156 BPM è −5.4 / +2.0 / −2.1 / −3.5 / −4.3 ms (media −2.6; allora +1.8).
- [x] Misura sull'audio vero (`VPTrack --player --quarters --out`: shaker a quarti, colpi separati). La salita del colpo:
  metà in ~4 ms dall'inizio, 80% in ~13 ms, picco ~19 ms. Il rilevatore a flusso di `onset_fit` data un attacco netto
  16.7 ms in anticipo (banco sintetico); corretto questo, la batteria fisica dei vostri live cade +11…+12.5 ms dopo il
  battito del maestro. Punto dell'80% dello shaker (dove il progetto considera «sentito» un colpo, `measureAttack`)
  rispetto alla batteria: con 12 ms, +5…+9 ms sui live, da −9 a +9 sugli studio; con 6 ms circa +1 ms sui live.
  Spostamento misurato sull'uscita: esattamente −6.0 ms.
- [x] **Tenuto 6 ms.** Il clock non si muove; banchi e `--phase-lock` misurano il clock e non cambiano. Il test del triangolo
  in `--percussion` leggeva finestre assolute tarate sulla vecchia trattenuta (passava per un soffio dall'altra parte):
  ora le legge dall'inizio vero del colpo, con valori identici a prima a 12 ms. `--percussion` 17/0.
- [ ] Ascolto su iPad (BRANO), poi dal vivo. Se ora sembra «davanti», 8–9 ms è la via di mezzo.

### 94. Pulizia del core: le «porte» del decoder e il ponte di moto via, il canale cassa resta 🟡 (2026-10-06/08, misurato — da ascoltare)

Richiesta: «verifica se c'è qualcos'altro da fare a livello di core o che non serve più e quindi meglio rimuovere».
- [x] Inventario: nessun interruttore sperimentale né codice spento nel tree (a parte i due temporanei di questa verifica,
  finiti nel commit `c119aad` e ora tolti). Restano: modalità LOOP (accesa di default, le conga vengono da lì),
  microfono dell'iPad (sorgente di partenza), armonia.
- [x] **Porte del decoder (A/B/C/D, i loro «hold», le guide IOI di clock, gli spostamenti d'origine):** tarate seme per
  seme sul banco sintetico del moto fra il 18 e il 23/09, prima della rete rifinita. Spente insieme sul banco vero (54
  esecuzioni, verità del maestro + batteria): scatti di velocità **0.31 → 0.18/min** in BRANO e **0.46 → 0.26** in MIXER
  −12 dB; disp 11.8 → 11.7 e 11.3 → 11.1 ms; oltre 25 ms 18.6 → 18.5% e 17.5 → 17.2%; ottava 78.3 → 78.2% e 77.3 → 76.9%
  (UNA CANZONE 44k, già sbagliata al 70%); nessun brano BRANO peggiore, 8 con meno scatti; in MIXER 12 con meno scatti.
  `VPAlign --steps/--ramps`, `VPTests --tempo-step` 14/0, `--tempo-slow` 13/0 identici. **Tolte** (−698 righe in
  `BeatDecoder`): identiche bit per bit alla versione spenta, in isolamento, 16/16. Banco sintetico `probe_motion_matrix
  --quick`: fisso identico, gradino 30.3 → 30.2 ms, **continuo 38.8/95.0 → 54.1/119.1 ms** (la famiglia per cui erano nate).
  `VPTests` `--bar` 13/0, `--new-input` 16/0, `--transport` 6/0, `--state-timing`, `--phase-lock` 20/0.
- [x] **Ponte di moto tolto** (2026-10-08, decisione dell'utente): `TempoMotionShape` + `TempoMotionTracker`
  (8 file, ~2100 righe), `motionBridgeAuthority`/`bridgedMotionTarget`, `kGridTauProvenMotion`, i campi diagnostici
  in ipotesi, tracker, motore e DEBUG (la riga «moto»), il flag `VPTests --tempo-motion`. Dove il decoder azzerava
  l'ombra resta solo ciò che faceva davvero: `dropIoiLead()`. I 3 test del follower ancora validi (hint «provato»,
  ora dato solo dall'IOI lead) sono passati in `VPTests --evidence` (5/0). Il `.pul` di `VPTrack` perde la 15ª
  colonna; il CSV di `probe_motion_matrix` le colonne `curve`/`recovery_violations`/`authority_frames`;
  `compare_motion_matrix.py` ora chiede fisso/gradino identici e continuo non peggiore (self-test PASS).
  - **Bit per bit identico a prima: no, e non può esserlo.** Su 54 file il ponte prendeva autorità in 14 (BRANO),
    18 (−6), 16 (−12), 16 (−18); ogni differenza comincia esattamente al primo frame con autorità, tutti gli altri file
    sono identici. Il controllo giusto è quello usato per le porte: un worktree di HEAD (`b4d95ad`) con l'autorità
    forzata a 0. **Rimozione = ponte spento bit per bit: 216/216** (BRANO, MIXER −6/−12/−18), e identici anche
    `stop_song.py` e `VPAlign --ramps`.
  - **Ponte spento contro acceso** (riferimento `r0*`, nuovo `r1*` in `~/vp-bench`):

    | | giusto % | disp ms | >25 ms % | uno % | scatti/min | uscite/min | file peggiori (`cmp`) |
    |---|---:|---:|---:|---:|---:|---:|---|
    | BRANO | 81.7 → 81.7 | 12.1 → 12.1 | 19.3 → 19.3 | 75.0 → 75.0 | 0.18 → 0.18 | 1.92 → 1.92 | nessuno |
    | MIXER −6 | 74.5 → 74.5 | 10.9 → 10.9 | 16.4 → 16.5 | 71.6 → 71.6 | 0.23 → 0.23 | 1.93 → 1.93 | nessuno |
    | MIXER −12 | 81.0 → 81.1 | 11.8 → 11.8 | 18.4 → 18.4 | 78.8 → 78.8 | 0.24 → 0.23 | 2.08 → 2.08 | nessuno |
    | MIXER −18 | 84.0 → 84.0 | 13.7 → 13.7 | 20.6 → 20.6 | 81.2 → 81.2 | 0.27 → 0.27 | 2.04 → 2.02 | nessuno |

    Per file solo decimi nei due sensi (es. UMBRELLA 48k BRANO >25 ms 16.8 → 18.6%, WRECKING BALL 44k −12 dB
    36.3 → 32.9%, FLAMINGO 1200 BRANO tempo giusto 91.2 → 92.7%).
  - `stop_song.py change` identico; `inside` STOP 4 s sbagliata totale 45.8 → 45.9 s (p90 e mediane identiche).
    `VPAlign --steps` identico; `--ramps` MIXER media 30 s 19.8 → 19.5, 12 s 35.5 → 36.1, 120→132 26.3 → 26.1 ms,
    peggio invariato, tutti PASS. `probe_motion_matrix --quick`: hash identici (lì il ponte non prendeva mai autorità).
  - `VPTests` `--tempo-step` 14/0, `--tempo-slow` 13/0, `--bar` 13/0, `--new-input` 16/0, `--transport` 6/0,
    `--phase-lock` 20/0, `--state-timing` PASS, `--evidence` 5/0.
- [x] **Canale cassa separato** (`KickOnsetDetector`, tasto CASSA, ~300 righe): **resta** (decisione dell'utente,
  2026-10-08: «potrebbe servire»). Spento di default; dal vivo oggi si usa il mix completo.
- [ ] Ascolto: con le porte tolte la parte dovrebbe «scattare» meno, soprattutto nei brani lenti e dal vivo.

### 95. Cambio suoni dei knob: pulsante EDIT e modale al posto del long-press 🟡 (2026-10-06, compila — da provare sul dispositivo)

Il tenere premuto 450 ms su un knob apriva il menu dei suoni; si apriva per sbaglio mentre si cercava di regolare il volume. Ora il cambio è un'azione esplicita: **EDIT** in alto a destra nella scheda FEEL.

- EDIT acceso (il testo diventa **FATTO**): i knob portano un anello tratteggiato fucsia; un tap su un knob apre una **modale centrata** («SUONI» per i quattro knob delle percussioni, «CAMPIONI» per i quattro one-shot) con solo ciò che nessun knob usa già, e la riga «al posto di …». Se non resta nulla di libero lo dice. Il trascinamento verticale regola ancora il volume.
- EDIT spento: tap = mute / one-shot come prima. Il long-press è stato rimosso (`VoiceKnob` non ha più timer né `onHold`).
- Codice: `MainComponent::setSoundEditMode`, `SoundMenuOverlay` (ora una modale, non più ancorata al knob), anello in `drawRotarySlider` (proprietà `editMode`).
- **Bug trovato al primo giro (2026-10-06):** EDIT c'era ma non si vedeva — `refreshThemeColours()` ricolora una lista fissa di pulsanti e EDIT non c'era, quindi teneva il testo scuro della costruzione (prima del tema scuro): nero su nero. Ora `refreshThemeColours()` richiama `setSoundEditMode()`, e EDIT è fucsia da spento, bianco con la barra fucsia da acceso. Verificato con cattura della finestra su Mac: «EDIT» fucsia in alto a destra nella scheda FEEL.
- **Icona (2026-10-06):** EDIT è ora un quadratino con una matita disegnata come path (proprietà `pencilIcon` in `drawButtonText`, come l'ingranaggio), niente più scritta EDIT/FATTO. Senza riquadro né barra: spenta la matita è bianca (colore del testo, scura nel tema chiaro), in modalità edit è fucsia. Titolo accessibile «Modifica suoni». Verificato con cattura della finestra su Mac.
- Resta da fare: provare il flusso (EDIT → tap su knob → modale → scelta) sul dispositivo, e guardare nel layout compatto che il pulsante non copra l'ultimo knob della riga alta.

### 96. Ottava all'ingresso: la parte entrava sul tempo dell'intro; banco reso deterministico 🟡 (2026-10-06, misurato — da ascoltare)

Richiesta: «continua sul lavoro sul core che varrebbe di più» (item 94: levare automatico, ottava all'ingresso, brani lenti con ottavi forti).
- [x] **Dove si perde:** sui 54 file il 12–14% dei battiti è all'ottava sbagliata, il levare circa l'1%. Lo stesso brano cambia ottava fra 44.1 e 48 kHz o fra BRANO e MIXER.
- [x] **La rete rifinita sa l'ottava:** attivazione a metà quarto su quella del quarto (verità del maestro): EVERYTIME, I WANNA DANCE, INFINITO, THE REASON, VITA 0.00–0.01; UNA CANZONE 0.03; UN ORA SOLA 0.19; SEE YOU AGAIN 0.17; solo VIVERE è davvero ambigua (0.55). La sezione «l'ottava è in parte irrisolvibile» della skill era misurata sulla rete di prima (0.73–0.77 a 76 BPM).
- [x] **Causa trovata (EVERYTIME):** l'intro viene letto a 62.6; entra la band (ripartenza fredda a 8.7 s), il worker riparte da zero ma il tracker resta «following» sul tempo dell'intro, e la parte entra a 9.5 s su 62.6 prima che la rete abbia letto un battito della band. La prima lettura vera (120 a 11.1 s) viene poi piegata su quell'ottava (60) dalla tenuta sotto la parte che suona, per tutto il brano. In MIXER l'intro a −12 dB non aggancia, quindi lì entrava giusto.
- [x] **Fix** (`BeatTracker::setInputEpoch`, `staleIntroLock`): una ripartenza fredda prima che la parte abbia mai suonato, con un aggancio in corso, scarta quell'aggancio come un file nuovo. Non dopo che la parte ha suonato (una pausa a metà brano tiene il suo aggancio: WRECKING BALL 44k a 51 s riagganciava a metà) e non senza aggancio (la prima ripartenza dal silenzio). Tempo giusto contro il maestro: **BRANO 78.2 → 81.6%** (EVERYTIME 44k e 48k 0 → 99%), **MIXER −12 dB 76.9 → 81.0%** (VIVERE ×2 0 → 85%, WRECKING BALL 44k 9 → 83%, THE REASON 48k 97 → 92%); tutti gli altri file identici bit per bit. Ingresso 1–3 s più tardi solo su quei file.
- [x] `VPTests` `--transport` 6/0, `--new-input` 16/0, `--bar` 13/0, `--state-timing` PASS, `--phase-lock` 20/0. `--level` 11/5 e `--octave` 7/4: gli stessi fallimenti con il fix spento (preesistenti; `--level` sono i casi 168 BPM a −12 dB e a clip e 91 BPM a clip).
- [x] **Banco deterministico** (il confronto sopra non si poteva fare senza): due esecuzioni della stessa build non sempre coincidevano (a 3 processi un file su 54 diverso; UNA CANZONE 48k −12 dB da sola 0.8% di tempo giusto, nel banco 85.9%). Due cause, entrambe solo nelle sonde offline: l'attesa di `VPTrack` scadeva a 400 ms (ora 10 s con `VP_OFFLINE_PACING`), e il worker poteva analizzare un blocco prima o dopo che il tracker gli dicesse se la parte suona (`NeuralBeatTracker::releaseFed`: con `VP_OFFLINE_PACING` l'audio passa al worker a fine blocco; sul dispositivo nulla cambia). Ora 54/54 identici fra due esecuzioni a 6 processi e fra banco ed esecuzione isolata. `bench_fast.py` accetta `JOBS=N`.
- [ ] Ascolto: EVERYTIME in BRANO deve entrare a ~124, non a 62, un paio di secondi dopo la band; dal vivo, i brani con un intro senza ritmica.
- [x] **UN ORA SOLA (BRANO), analizzato:** la rete nell'app dice 76 con chiarezza (attivazione a metà quarto 7% di quella sul quarto dopo 24 s). La parte entra a 24.1 s, quando entra la batteria: in quell'istante il decoder salta da 90 a 153 (confidenza 0.23) e anche il pettine dice 153. Il pettine diventa netto su 76 solo a 26.5 s, e resta netto per quasi tutto il brano; il decoder però lo ripiega sull'ancora (`BeatHmm`, `foldToAnchor`), che dice 150, e con la parte che suona l'ottava è comunque tenuta. In MIXER guarisce perché a 26.5 s la parte era ferma.
- [x] Provato e scartato: **togliere l'ancora** (`setLevelAnchor(false)`). Tempo giusto BRANO 81.6 → 80.0%, MIXER 81.0 → 80.5%: guariscono I WANNA DANCE 44k, UNA CANZONE 44k, FLAMINGO 2400 (MIXER), si rompono UMBRELLA ×2 e SEE YOU AGAIN 44k (MIXER) e VIVERE; UN ORA SOLA in BRANO resta a 153.
- [x] Misura (pettine contro ancora, su tutti i file): un pettine **deciso** (≥5 volte l'ottava dell'ancora) **per 8 s di fila** ha ragione su UN ORA SOLA (×4, fino a 64 s di fila), FLAMINGO 2400 (MIXER) e VIVERE (BRANO); ha torto solo su GARDEN 1500, file con verità inaffidabile. Ma quasi sempre succede **con la parte che suona**.
- [x] **Decisione dell'utente (2026-10-06): sì alla correzione automatica, misurata → item 97.** Era: correggere UN ORA SOLA vuol dire o un ingresso più lento per tutti i brani (≥ 8–10 s dopo la batteria), o **una correzione automatica dell'ottava dopo l'ingresso**, che oggi la regola vieta (solo a mano). Proposta: una sola volta, solo nei primi ~20 s dopo l'ingresso, solo con il pettine deciso per 8 s di fila. Senza il sì dell'utente non si fa: resta il ÷2/×2 a mano.

### 97. Una sola correzione automatica dell'ottava, nei primi 20 s dopo l'ingresso 🔴 (2026-10-06, TOLTA — vedi item 100)

Decisione dell'utente dopo l'item 96: «sì, prova la correzione automatica e misurala». Regola globale, nessun valore per brano.
- [x] **Regola** (`BeatDecoder::updateEarlyOctaveFix`): con la parte che suona, se il pettine dà alla sua ottava almeno **5 volte** il punteggio dell'ottava tenuta, **per 8 s di fila**, a livello assestato, e la serie si completa entro **20 s** dal primo ingresso della parte nel brano, il voto d'ottava già esistente viene lasciato passare **una volta**. Mai dopo un ÷2/×2 a mano; si azzera con un brano nuovo (`notifyInputRestart`). Da armata scavalca il rifiuto «pettine rimasto dopo un buco» e il veto `unprovenSlowerOctave` (8 s di pettine deciso sono la prova). Il tracker riconosce quella griglia (`BeatHypothesis::earlyOctaveFixSerial`) e non la riporta indietro (`holdSoundingLevel`), come fa con ogni altro salto d'ottava sotto la parte.
- [x] **Misura, 54 file contro il maestro:** tempo giusto **BRANO 81.6 → 82.3%**, **MIXER 81.0 → 81.5%**; cambiano 4 file su 108, gli altri identici bit per bit. **UN ORA SOLA** 44k e 48k (BRANO) **0 → 87% e 91%** (ingresso a 24.1 s su 153, passa a 76.9 a 39.5 s, poi resta 74–75), **FLAMINGO 2400** (MIXER) **60 → 84%**; **VIVERE 48k** (BRANO) 87 → 0.3%: era a 54 (né 65 né 130), il pettine deciso a 108 l'ha portato a ~130, l'ottava che le altre tre versioni di VIVERE avevano già. VIVERE è il brano che la rete legge davvero ambiguo (attivazione a metà quarto 0.55). Scatti di velocità BRANO 0.18 → 0.19/min, MIXER 0.24 → 0.23.
- [ ] Ascolto: UN ORA SOLA in BRANO suona ~15 s a 153 e poi passa a 76 per il resto del brano; VIVERE ora sempre a ~130 (se va contato a 65, ÷2).

### 98. Controllo regressioni contro la 1.2.6 (`be0315f`) e correzione di UNA CANZONE 44k (MIXER) 🔴 (2026-10-06, correzione TOLTA con la 97 — vedi item 100; il controllo regressioni resta valido)

Richiesta: «analizza se segue al meglio e stabilmente i brani o se è subentrato qualche bug o regressione… rispetto alla versione con tag 1.2.6». 1.2.6 ricompilata a parte, stessi 54 file, stesso banco deterministico.
- [x] **Medie, 1.2.6 → ora:** tempo giusto BRANO 78.3 → **82.3%**, MIXER −12 dB 76.7 → **82.4%**; primo quarto («uno») 69.7 → 70.4% e 73.0 → **76.9%**; scatti di velocità 0.31 → **0.19/min** e 0.45 → **0.23/min**; precisione sui brani alla stessa ottava 12.4 → 12.2 ms e 11.7 → 11.6 ms (oltre 25 ms 19.0 → 18.9% e 18.0 → 17.8%).
- [x] **Nessuna deriva:** scarto fra ultimo e primo quarto di ogni brano, mediana +0.3 ms (BRANO) e +0.9 ms (MIXER), come nella base di inizio sessione. Lo spostamento di «anticipo» medio in MIXER (23.5 → 29.7 ms) è composizione della media: brano per brano lo scarto mediano è −0.2 ms, e sugli attacchi della batteria −0.3 ms.
- [x] **Peggiorano davvero:** VIVERE 48k (BRANO), ottava ambigua, ora ~130 come le altre tre versioni (item 97); THE REASON 48k (MIXER) 97 → 92%, costo della correzione dell'intro (item 96: l'intro diceva 85, la band riaggancia a 59 per ~15 s); ingresso più tardi di 1–6 s dove l'intro aveva un aggancio (FEEL 44k MIXER 7.1 → 13.1 s, ma prima entrava a 133 su un brano a 104). Primo quarto: 5 brani meglio, 5 peggio (bilico 1/3), media in salita.
- [x] **UNA CANZONE 44k (MIXER) 0 → 70%** (era 89% nella 1.2.6, rotta dalla rimozione delle porte, item 94): entra a 116 (4:3 del vero 85.7), l'analisi la porta da sola a 151 e poi 168 (il doppio) a 22.7 s, due secondi prima che la finestra dell'item 97 contata dall'ingresso scadesse. Due ritocchi a `updateEarlyOctaveFix`, globali: (1) uno spostamento del tempo della parte oltre ~11% che non sia un'ottava (stessa soglia di `kAnalysisJump` nel tracker) fa ripartire la finestra di 20 s — la parte cambia tempo comunque lì; (2) un campione con il pettine non assestato non conta né azzera la serie (il flag cadeva per mezzo secondo a 53 s). Solo quel file cambia: MIXER 81.5 → 82.4%, BRANO identico bit per bit. `VPTests` `--transport` 6/0, `--new-input` 16/0, `--bar` 13/0, `--phase-lock` 20/0.
- [x] **STOP** (`stop_song.py`): cambi di brano identici all'item 92 (START +0.5 s: 1.9 s, +2 s: 0.9 s). STOP 8 s a metà brano p90 0.7 → 3.5 s, sbagliata 53 → 68 s: un solo brano, UN ORA SOLA 48k, che ora segue a 76 e incontra un limite della regola dell'item 92 — lo STOP cade su un passaggio piano (attivazione 0.2–0.5 da 58.7 a 62 s), nessun battito per ~5 s, la regola lo prende per fine brano e riparte; il riaggancio sbaglia (60) per ~15 s. Provato a decidere sui battiti mancanti *al momento* dei 3 s invece che memorizzati: STOP 4 s sbagliata 45.8 → 54.0 s e START +2 s 0.9 → 3.8 s, scartato. START 1 s *prima* dell'attacco con pause di 2–4 s: 12 s di attesa in silenzio, uguale nell'item 92 (STOP più corto dei 3 s richiesti).
- [ ] Ascolto: UNA CANZONE 44k in MIXER.
- [ ] Possibili miglioramenti (non fatti): riaggancio dopo la ripartenza (THE REASON 59, UN ORA SOLA 60 dopo lo STOP — stessa famiglia: una lettura fresca su un reticolo 2:3); START premuto prima dell'attacco con STOP breve.

### 99. Riaggancio dopo una ripartenza: l'aggancio dell'intro si tiene se è dimostrato 🔴 (2026-10-06, TOLTO — vedi item 100)

Richiesta: «migliora il riaggancio dopo una ripartenza» (item 98: THE REASON 48k riagganciato a 59, UN ORA SOLA a 60 dopo lo STOP, FEEL a 69).
- [x] **Causa (THE REASON 48k, MIXER):** l'intro è agganciato bene (84.7, fit lunghi pieni, pettine d'accordo, confidenza 0.97); all'ingresso della band l'item 96 lo scarta, il decoder riparte e 1 s dopo aggancia 59.1 con confidenza 0.52 e senza pettine, la parte entra lì; il pettine dice 85 da 23.6 s, il decoder ci passa a 30 s.
- [x] **Misura al momento dello scarto (tutti i file):** gli agganci d'intro giusti (THE REASON ×2, BLUE SKY 48k, EVERYTIME MIXER ×2) hanno tutti fit lungo + copertura 1.00 + pettine d'accordo; quelli sbagliati no (VIVERE copertura 0.25, WRECKING BALL 0.29, FEEL ed EVERYTIME file senza fit lungo).
- [x] **Fix** (`BeatTracker::setInputEpoch`, `provenLock`; `lastHyp` tiene l'ultima ipotesi periodica): l'aggancio dell'intro si scarta solo se non è dimostrato (copertura ≥ 0.9, fit lungo e pettine entro il 4% del tempo). Tempo giusto MIXER 82.4 → **82.5%** (THE REASON 48k **92 → 97%**, BLUE SKY 48k 60 → 61%), BRANO identico bit per bit; ingressi tornati ai tempi della 1.2.6 (EVERYTIME MIXER 11.1 → 9.1 s, THE REASON 48k 17.4 → 16.4 s, BLUE SKY 48k 54.8 → 53.5 s). `stop_song.py` identico; `VPTests` `--transport` 6/0, `--new-input` 16/0, `--bar` 13/0, `--phase-lock` 20/0, `--state-timing` PASS.
- [x] Provati e scartati: **non azzerare la rete** a una ripartenza fredda su audio continuo (BRANO 82.3 → 80.6%, MIXER 82.4 → 79.1%: guariscono THE REASON 48k e UNA CANZONE 44k, si rompono VIVERE ×2, WRECKING BALL 44k, UNA CANZONE 48k, FLAMINGO 2400); **primo ingresso solo con il pettine d'accordo** (stesso tempo o un'ottava) (BRANO +0.1, MIXER 82.5 → 81.3%, ingresso ritardato fino a 27 s, p90 3–4 s; FEEL e WRECKING BALL invariati).
- [ ] Resta: UN ORA SOLA dopo uno STOP di 8 s su un passaggio piano (item 98) — anche una fine brano vera ha un aggancio dimostrato prima, quindi questa regola non li separa. FEEL e WRECKING BALL 04 (BRANO) entrano su un primo aggancio sbagliato senza ripartenza: altro problema (acquisizione iniziale).

### 100. Verifica su brani mai visti: tenuta la 96, tolte 97–99; un difetto della 97 trovato 🟡 (2026-10-06, misurato)

Richiesta: «non mi interessa che migliori le acquisizioni per brani specifici. Mi interessa che sfrutti i brani per capire e migliorare globalmente» → «procedi».
- [x] **Metodo:** banco di verifica = i 46 file (23 brani × 44k/48k) da cui **non** è nata nessuna regola di oggi (fuori UN ORA SOLA, UNA CANZONE, THE REASON, EVERYTIME). Per ogni regola: tutto acceso meno quella regola, effetto sulla media del banco di verifica (BRANO e MIXER −12 dB, 92 esecuzioni). Si tiene solo ciò che lo migliora in modo netto.
- [x] **Esito sulla verifica:** item 96 (scarto dell'aggancio dell'intro) MIXER 4 file cambiati, tempo giusto **+4.7**, uno +4.3, scatti −0.04/min → **tenuto**. Item 97 (correzione d'ottava) 1 file su 92 (+0.58 MIXER) → **tolto**. Item 98 (finestra che riparte, campione non assestato) 0 file → **tolto**. Item 99 (aggancio «dimostrato») 1 file, giusto +0.01, uno −0.32 → **tolto**.
- [x] **Difetto trovato nella 97 durante la verifica:** il tracker accettava come «correzione ammessa» `earlyOctaveFixSerial == gridSerial`, vero anche con entrambi a 0 a inizio brano: la tenuta d'ottava sotto la parte era spenta finché la griglia non veniva ricostruita (VIVERE 48k BRANO 87 → 0.3%; media BRANO falsata di −1.5 punti nelle misure degli item 97–99). Sparito con la 97.
- [x] **Stato finale** (rispetto al commit `8acfc4b` restano solo item 96 e `releaseFed`): tempo giusto BRANO **81.6%**, MIXER **81.0%** (1.2.6: 78.3% e 76.7%); `stop_song.py` come item 92 (STOP 8 s p90 0.7 s, 53 s); `VPTests` `--transport` 6/0, `--new-input` 16/0, `--bar` 13/0, `--phase-lock` 20/0, `--state-timing` PASS. UN ORA SOLA torna a 153 in BRANO (÷2 a mano).
- [x] Regola salvata in memoria e nella skill: ogni regola si convalida su file da cui non è nata.

### 101. Skill del tempo snella + `tempo-bench` + `CLAUDE.md`, dalla PR #2 ✅ (2026-10-07, solo documentazione)

Dalla PR nikuniku74/VirtualPerc#2 (basata sulla 1.2.6) portato sul ramo di lavoro solo ciò che migliora:
- `realtime-tempo/SKILL.md` da 5.300 righe a ~190 (regole, schema del decoder, numeri, mappa); sezioni 2–6 e 8 in `references/`, **rigenerate dalla skill di questo ramo** (comprese le note degli item 94–100: 4763 righe, nessuna persa). Numeri della §4 aggiornati allo stato attuale (item 100), aggiunte la regola dell'aggancio dell'intro (item 96), la nota sulla rete rifinita e la convalida su brani mai visti.
- Nuova skill `tempo-bench`, corretta: worktree con `third_party/onnxruntime` collegato (non `git checkout <sha> -- Source/`, che cancella il lavoro non salvato), determinismo vero solo da item 96, fallimenti noti `--level` 11/5 e `--octave` 7/4, verifica su metà banco mai vista.
- `CLAUDE.md` (importa `AGENTS.md`), `AGENTS.md` e la regola Cursor come nella PR.
- Non portato: niente (il resto della PR era solo spostamento).

### 102. Rete: secondo giro di addestramento con i brani nuovi dell'utente 🔴 (2026-10-07, misurato — nessun modello nuovo adottato)

Dati nuovi (`~/Desktop/clicks/nuovi`: 9 brani, FBI 17.11.11, Nonsoulfunky 18.04.2009 2P): 3.9 h, ~22 900 battiti, in `~/vp-train/new` (wav mono 44.1 kHz, verità del maestro, `.f32`/`.act` con `VP_BEAT_MODEL=gtzan_orig.onnx`, ricetta verificata identica ai file esistenti). Contaminazione (inviluppo d'attacco, 2 sonde da 60 s per brano del banco): massimo 0.45 (GARDEN 2400 dentro FBI: stesso repertorio, non stesso audio; copia = 0.71), NonSoulFunky 2009 contro il 2015 del banco < 0.20. `~/vp-train/plus` = `wav` + `new` (link).
- [x] **L'addestramento è deterministico:** `--data ~/vp-train/old --steps 1000 --kd 3 --kdoff 0.3 --seed 0` rifà `m1k3o03s0` (il modello dell'app) bit per bit. Quindi il modello dell'app è addestrato sulle 4 h.
- [x] **Tre insiemi × 4 semi**, stessa ricetta, banco di 54 file contro la verità (media [min–max] sui semi), tempo giusto %:

| | A: 4 h | B: 9.6 h | C: 9.6 + 3.9 h nuove |
|---|---|---|---|
| BRANO | 74.9 [72.2–81.7] | 77.3 [74.8–81.8] | **80.8 [80.3–81.3]** |
| MIXER −12 dB | **81.0 [78.2–83.5]** | 78.2 [76.7–79.7] | 76.2 [74.1–79.6] |
| uno BRANO / MIXER | 66.5 / 74.6 | 67.8 / 72.3 | 71.7 / 74.2 |
| disp ms BRANO / MIXER | 12.0 / 12.7 | 13.1 / 12.8 | 13.8 / 12.1 |

  Uguale senza GARDEN 2400. Più dati: **BRANO meglio e molto più stabile fra i semi** (intervallo 9.5 → 1.0 punti), uno +5; ma **MIXER peggio di ~5 punti**, ed è il modo dal vivo. Il modello dell'app (A seme 0: 81.7 / 81.0) è il seme fortunato di A; nessun modello nuovo lo batte in entrambi i modi → **non cambiato**.
- [x] **Ipotesi provata e respinta:** il MIXER peggiora perché la rete impara le feature al livello del file? Feature d'addestramento rigenerate attraverso la catena MIXER del motore (VPTrack `--gain -12`, rete originale per i `.act`; scrittura temporanea dei fotogrammi dal worker, poi tolta), fotogramma per fotogramma allineate a quelle del file (stesso numero, ±1 in coda), ogni brano due volte (`~/vp-train/plusmx`). D × 4 semi, tempo giusto: BRANO **77.7** [75.2–79.4] (C 80.8), MIXER **76.0** [73.9–79.2] (C 76.2), uno 69.5 / 68.9 (C 71.7 / 74.2). Peggio o uguale ovunque: il calo in MIXER non viene dalle feature.
- [x] **Correzione dell'utente (2026-10-07):** tutti i file in `~/Desktop/clicks` e `nuovi` sono registrazioni live dalla mandata del mixer (anche i brani del banco): i dati d'addestramento sono già del dominio giusto, e il banco intero è uso dal vivo.
- [x] **Solo i 12 tratti live Garden/Flamingo**, tempo giusto BRANO / MIXER: modello dell'app **86.1 / 84.8**; A 74.9 / 78.4; B 77.2 / 73.0; C 82.1 / **63.7**; D 79.0 / 69.6 (medie di 4 semi). Il modello dell'app vince ovunque.
- [x] **Seconda ipotesi respinta:** il make-up del MIXER a parte suonante (0.40, item 90, scelto con il modello dell'app). Riportato a 0.20 (interruttore temporaneo, tolto): modello dell'app 81.0 → 80.9, C 76.2 → 76.0. Non è il make-up.
- [x] **Conclusione:** con questi dati l'addestramento non dà un salto: il modello dell'app (4 h, seme 0: 81.7 / 81.0) resta il migliore in entrambi i modi; più dati rendono BRANO stabile ma costano il MIXER, con qualunque feature. Probabile che il percorso MIXER del decoder sia tarato su questo modello (item 90, make-up 0.40 scelto con esso). Perché un modello nuovo perdesse in MIXER: vedi sotto.
- [x] **Diagnosi (2026-10-07):** in MIXER i modelli C perdono quasi tutto sull'**ottava** (FLAMINGO 4500 99.5% in BRANO, 49.8% in MIXER), cioè sulla decisione presa in acquisizione, dove il MIXER analizza a 0.20 (make-up prima che la parte suoni) e il BRANO al livello del file (~0.43 sulle mandate). Livello d'acquisizione MIXER (interruttore temporaneo, tolto), tempo giusto: modello dell'app 0.20 **81.0** / 0.30 79.1 / 0.43 77.4; C (4 semi) 0.20 76.2 / 0.30 79.5 / 0.43 **82.7** [80.1–84.9] (ottava sbagliata 13.6 → 7.4%, uno 75.9). **La scelta d'ottava dipende dal livello d'analisi in acquisizione e ogni rete ha il suo ottimo**: quella dell'app a 0.20 (scelto con lei, item 90), le nuove al livello dei loro dati. Il calo «non spiegato» era questo.
- [ ] Prossimo passo proposto: addestrare con **variazione di livello** (feature calcolate dallo stesso audio a più guadagni), così la rete non dipende dal livello e BRANO e MIXER decidono uguale; poi scegliere modello e livello sul banco intero con la verifica su metà mai vista.

### 103. Banco spostato in `~/vp-bench` ✅ (2026-10-07)

`/tmp/vp-bench` è stato svuotato due volte in un giorno (pulizia periodica di macOS dopo 3 giorni e riavvio). Gli script (`bench_fast.py`, `truth.py`, `stop_song.py`, `bench_songs.py`, `surge_scan.py`, `surge_sources.py`) usano ora `~/vp-bench`; banco ricostruito lì (54 WAV, verità del maestro rigenerata: le eventuali correzioni a mano della verità fatte in passato sono perse). Ricetta di ricostruzione nella skill `tempo-bench` §0. Riferimento attuale: BRANO 81.7%, MIXER 81.0% di tempo giusto.

### 104. Rete addestrata con variazione di livello; l'ottava è una decisione sul filo 🔴 (2026-10-07, misurato — nessun cambiamento all'app)

- [x] **Dati:** aggiunto «12 tappeto di fragole» (live, mandata). Per ogni file d'addestramento feature anche a livello d'analisi 0.10 / 0.20 / 0.40 (`VPActivations --gain`, guadagno dal 75° percentile dei picchi al secondo), ogni brano 4 volte (`~/vp-train/plv`). `train_beatnet_finetune.py` legge le feature con `np.memmap` (identico bit per bit: rifà `m1k3o03s0`). **Il Mac (19 GB) si è bloccato** con 5 `VPActivations` in parallelo sui set da 1–2 h: ora i set lunghi uno alla volta.
- [x] **E × 4 semi**, tempo giusto (banco intero / solo tratti live): BRANO 76.0 / 82.8 (app 81.7 / 86.1); MIXER −6 dB 76.9 / 85.2 (app **74.5 / 75.6**); −12 dB 79.6 / 80.4 (app 81.0 / 84.8); −18 dB 81.7 / 87.3 (app 84.0 / 83.4). Più stabile fra i livelli, peggio in BRANO e a −12 → non adottato.
- [x] **Make-up simmetrico in MIXER** (abbassare anche le mandate calde a 0.20; interruttore temporaneo, tolto): identico a −6/−12/−18. Una mandata da ~0.43 a −6 dB arriva già a ~0.21: non era il livello d'analisi.
- [x] **Diagnosi:** fra −6 e −12 dB, stesso modello, ~10 file cambiano ottava **nei due versi** (WRECKING BALL 44k e UNA CANZONE 48k giusti a −12, WRECKING BALL 48k e I WANNA DANCE 44k giusti a −6; VIVERE, UN ORA SOLA, SALLY, GARDEN 600). Il calo a −6 è il saldo dei capovolgimenti. Come per seme, livello e 44.1/48 kHz: su quei brani la scelta d'ottava è in parità e la decide un dettaglio. Una rete nuova sposta solo dove cadono.
- [ ] **Proposta:** rendere stabile la scelta d'ottava quando le prove sono in parità (stessa risposta a ogni livello, seme, frequenza), misurando la stabilità con le repliche −6/−12/−18 dB e 44.1/48 kHz; convalida su metà banco mai vista.

### 105. Stabilità della scelta d'ottava: senza ancora la decisione non dipende più dal volume della mandata 🔴 (2026-10-07, misurato — non adottata: non regge su brani mai visti)

- [x] **Misura di stabilità** (`~/vp-bench/tools/stab.py`): ogni brano in BRANO e MIXER a −6/−12/−18 dB, 44.1 e 48 kHz (fino a 8 repliche); classe dominante del tempo per replica. Oggi: tempo giusto 81.7 / 81.0 / **74.5** / 84.0 (media 80.3), 27/33 brani stabili, 6 instabili (VIVERE, WRECKING BALL 04, UN ORA SOLA, I WANNA DANCE, UNA CANZONE, SALLY): lenti al doppio, veloci a metà.
- [x] **Spareggio per tempo preferito respinto prima di provarlo:** sul repertorio d'addestramento (indipendente dal banco) il maestro ha mediana 118.6 BPM, larghezza 0.31 ottave — è già la preferenza del modello a stati (118, 0.40). Un centro più basso verrebbe dal banco: taratura. Preferenza più debole (larghezza 0.80): media 76.6, 23/33 stabili → peggio.
- [x] **Senza ancora** (`setLevelAnchor(false)`; il modello a stati non sceglie più l'ottava, decide il pettine sull'attivazione della rete rifinita). Banco intero: 80.0 / 80.5 / **80.3** / 79.6 (media 80.1), stabili 27/33, spariscono i capovolgimenti di VIVERE, UNA CANZONE, I WANNA DANCE, SALLY. **Tratti live** (silenzio prima, come dal vivo): BRANO 86.0 (86.1), MIXER −6 **87.5** (75.6), −12 **86.9** (84.8), −18 **87.3** (83.4); uno 69.4/73.9/67.7/74.4 (70.8/60.5/71.5/74.4); nessuna parte muta.
- [x] **Difetto di «senza ancora»:** sui brani che partono già a pieno volume (START con la band in corso, item 88) la parte a volte non entra mai (UMBRELLA 4 repliche su 8, INFINITO e ASPETTANDO 1): senza ancora la confidenza perde il margine del modello a stati (`scoreConfidence`) e la regola `lineLocked` (confidenza > 0.80 per 4 s di fila) non si chiude.
- [x] **Variante: togliere solo il ripiegamento del pettine sull'ancora** (`foldToAnchor`; il modello a stati resta per l'aggancio rapido e la confidenza). Nessuna parte muta. Banco intero: 82.5 / 81.5 / 78.3 / 82.7 (media **81.3**, oggi 80.3), uno BRANO 71.9 (69.5), M6 71.7 (67.4); tratti live 86.5 / 84.6 / 82.2 / 86.2.
- [x] **Ma la verifica su brani mai visti non regge** (fuori i 6 brani instabili da cui è partito il lavoro, 43 file): BRANO 86.0 → 86.1, MIXER −12 **85.3 → 84.2**, −6 85.3 → 84.9, −18 84.7 → 84.7; uno −0.3/−1.1/+1.0/−0.1. Tutto il guadagno sta nei 6 brani (MIXER −6 27.4 → 49.7, ma −18 80.7 → 73.6). **Non adottata.** Limite del metodo: una correzione di stabilità migliora solo i brani instabili, e non ce ne sono di «mai visti» per dimostrarla; servirebbero registrazioni nuove con ottava in bilico.

### 106. Il primo quarto: memoria dei voti più corta 🟡 (2026-10-07, misurato, scelto ×0.95 dall'utente — da ascoltare)

- [x] **Diagnosi** (voti per battito registrati dall'app, interruttore temporaneo; verità del maestro): uno giusto 72.1% BRANO, 78.2% MIXER −12. Errori: 1↔3 55% / 78%, un quarto il resto; solo 10–12% nei primi 20 s; 76–82% in tratti di almeno 8 battute. **Tetto con un allineamento fisso per brano: 72.7 / 74.7%** (l'app è già lì); riallineando ogni 4 battute 88.4 / 89.4% → l'uno vero si sposta a metà brano (battute irregolari, levare, sezioni) e l'app lo segue tardi: memoria dei voti ×0.982 per battito (metà in ~38 battiti), 32 battiti per spostare.
- [x] **Memoria più corta** (`kVoteDecay`, con i battiti per spostare `0.58/(1−decay)`, al più 32; la pausa dopo una rotazione 9.6 → 4.8 s non cambia nulla). Banco diviso per brano, scelta sulla metà A, verifica sulla B. Uno giusto BRANO / MIXER:

| | A | B (verifica) | rotazioni/min | rotazioni che correggono / rompono |
|---|---|---|---|---|
| ×0.982 (oggi) | 66.8 / 70.8 | 73.0 / 82.6 | 0.27 / 0.21 | 48 / 1, 32 / 4 |
| ×0.97 | 71.3 / 72.1 | 75.1 / 84.1 | 0.32 / 0.27 | |
| ×0.95 | 72.6 / 74.1 | 78.2 / 85.1 | 0.39 / 0.45 | 56 / 14, 49 / 19 |
| ×0.93 | **74.0 / 75.3** | **79.3 / 85.1** | 0.50 / 0.53 | 62 / 24, 50 / 24 |
| ×0.90 | 74.1 / 73.4 | 77.7 / 82.3 | 0.82 / 0.85 | |

  Tempo, fase e scatti invariati (la rotazione cambia solo il conteggio). Migliora su entrambe le metà: è globale. **Costo udibile:** più rotazioni, e una parte rompe un uno giusto (il pattern salta di un quarto). Da scegliere con l'utente fra ×0.95 e ×0.93, poi ascolto.
- [x] **Scelto ×0.95** (utente). `BeatTracker`: `kVoteDecay` 0.982 → 0.95 per i voti della rete; `kBeatsToMoveTheBar` 32 → 11.6 (stessa quota del limite del contatore, dodicesimo battito); `kBeatsToTrustTheBar` e `kBeatsToTrustReentry` 7 → 6.6 (di nuovo l'ottavo battito, come dice il loro commento: con 7 il rientro dopo un buco perdeva la finestra di due battute, `--bar`); l'armonia tiene 0.982 (`kHarmonyVoteDecay`); l'uno dichiarato dall'utente parte ancora da 32 voti (`kLockedBarVotes`).
- [x] **Misura finale** (54 file contro il maestro): uno giusto BRANO **69.5 → 75.0%**, MIXER −12 **75.7 → 78.8%**; metà di verifica 73.0 → 78.2 / 82.6 → 85.3; tempo, fase, scatti, uscite identici; rotazioni 0.27 → 0.39 / 0.21 → 0.44 al minuto.
- [x] `VPTests` `--bar` 13/0 dopo aver corretto il test del «fill»: teneva lo spostamento dell'uno per sempre e chiedeva 4 battute ferme (cioè la memoria vecchia di 10 battute); ora è un fill di una battuta che torna, e il conteggio non lo segue. `--new-input` 16/0, `--transport` 6/0, `--phase-lock` 20/0, `--state-timing` e `--harmonic-entry` PASS.
- [ ] Ascolto: l'uno dovrebbe tornare giusto in poche battute dopo una battuta irregolare o un cambio di sezione; il pattern si sposta un po' più spesso (circa una volta ogni 2–2.5 minuti invece di 4), a volte su un uno che era giusto.

### 107. Tema chiaro: ingranaggio invisibile, scheda BPM scura 🟡 (2026-10-08, da compilare e guardare)

Segnalato dall'utente dopo la prima compilazione del restyle (`docs/UI_RESTYLE_HANDOFF.md`).
- [x] **Ingranaggio SETUP invisibile nel tema chiaro.** `vp::copySystemGear` disegnava con `kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Little` (in memoria A,B,G,R) in un'immagine JUCE ARGB (B,G,R,A): JUCE leggeva l'alfa dal rosso. Glifo bianco (scuro) rosso = alfa, funzionava; glifo nero (chiaro) rosso = 0, trasparente. Ora `PremultipliedFirst`.
- [x] **Scheda BPM uguale al tema scuro** (richiesta utente): in `paintStage` la scheda e tutto quello che c'è dentro (numero, orb, BPM, riga del tempo, quattro quarti) si disegnano con i colori scuri anche nel tema chiaro; tolti gli alfa ridotti per il chiaro (bloom ×0.55, alone 0.16).
- [ ] Guardare sul dispositivo nel tema chiaro. Restano col tema chiaro: i pulsanti ±/BPM sotto FISSO (dentro la scheda, sono componenti) e il colore di stato del bloom (verde/ambra/rosso nella versione chiara, leggermente più scura).

### 108. Consumo: niente ridisegno a tutto schermo a 15 Hz; CoreML contro CPU e carico dell'analisi misurabili 🟡 (2026-10-08, compila iOS e Mac — da misurare sul dispositivo)

- [x] **Ridisegno mirato** (`MainComponent::timerCallback`, in fondo). Prima un `repaint()` incondizionato ridisegnava tutta la finestra (sfumature della pagina, card, tutti i fader) 15 volte al secondo anche a band ferma, con il renderer CPU di iOS. Ora due chiavi: *pagina* (gradino del bagliore, colore di stato, flash del tap, tema, DEBUG) → tutta la finestra; *palco* (stato, BPM al decimo, orb, bloom, riga del tempo, l'1 dichiarato, parte) → solo `stageArea()`; niente cambiato → niente. MIC, fader `hitLit` e la pagina SETUP si ridisegnano da soli quando cambiano. Ciò che `paint`/`paintStage` legge dal timer deve entrare nella sua chiave, altrimenti si aggiorna solo quando si muove altro.
- [x] Il bagliore della pagina segue il livello mediato su ~1 s in 8 gradini (prima il picco grezzo di ogni tick, che obbligava al ridisegno totale a ogni tick con la band che suona).
- [x] **CoreML contro CPU**: mai misurato. `VP_NO_COREML=1` (schema Xcode → Environment Variables) costruisce la sessione solo CPU. DEBUG mostra `rete X ms CoreML|CPU` (media ~1 s del tempo di una chiamata della rete; budget 20 ms per frame), anche nella riga di log `VPLAG`.
- [x] **Carico dell'analisi sul dispositivo**: DEBUG (e `VPLAG`) mostra anche `analisi N% core`, la quota di un core che il worker passa a lavorare (feature + rete + decoder) nell'ultimo secondo, accanto a `lead` e `rete`. Se `lead` cresce durante il set, questi due dicono se è la rete, il resto dell'analisi o la termica (item 74).
- [x] **Tensori della rete creati una volta** (`OnnxSession::run`): ingresso e stato LSTM (h, c, hn, cn) sono `OrtValue` costruiti alla prima chiamata e riusati; prima 3 tensori creati e 3 uscite allocate a ogni frame. Solo i logits restano allocati da ONNX Runtime (la forma è del modello). Attivazioni identiche bit per bit su 3,000 frame (`VPActivations` prima/dopo); sul Mac il tempo non cambia (0.23 s utente entrambi): il guadagno, se c'è, è sul dispositivo e si legge in `rete`.
- [ ] Sul dispositivo: Instruments (Time Profiler / Energy) prima e dopo il ridisegno mirato, a band ferma e con la band; guardare che nulla resti «vecchio» sullo schermo (BPM, orb, stato, MIC, fader one-shot, SETUP, rotazione).
- [ ] Sul dispositivo: `rete` con e senza `VP_NO_COREML`, a freddo e dopo 20 minuti; tenere il più veloce e stabile come predefinito.

### 109. STOP immediato: tolto il «tieni premuto» 🟡 (2026-10-08, compila — da provare sul dispositivo)

Richiesta utente: STOP si deve fermare subito al tocco. Prima era un press-and-hold di 0.5 s con la barra rossa (`StopHold`, pensato contro il pollice vagante a metà brano). Tolti `StopHold`, `startFiredOnPress`, la barra `holdProgress` e la scritta «TIENI PREMUTO»: lo stesso pulsante ora fa START o STOP al tocco (`setTriggeredOnMouseDown`). Il battito sul pulsante armato resta.
- [ ] Sul dispositivo: un tocco ferma; un doppio tocco veloce su START non deve fermare subito (se succede, serve un piccolo intervallo minimo dopo START).

## Standby

Lavoro **non bloccante** se usi solo **PATTERN** (motore sintetico / `GrooveEngine`, switch LOOP spento). Il codice del ciclo Codex (tempo rapido, suddivisione congas, canceller, epoch/make-up, 156 BPM, test) è già nel tree; qui resta la **chiusura formale** e l'integrazione **loop registrati** (altro documento).

### 39. Cambio violento: il buco fra transizione e ottava è chiuso sulla mandata 🟢 (2026-09-10)

Il percorso rapido accettava al massimo il 25% e solo entro un quarto d'ottava;
quello d'ottava, correttamente, decideva soltanto relazioni metriche. Un salto
come 120→160 non apparteneva a nessuno: arrivava al watchdog dopo **23.6 s**.

Su linea/mixer il detector può ora accettare fino al 65%, ma solo lontano almeno
0.15 ottave da una relazione ×2/÷2 e dopo **tre** intervalli causali coerenti.
Restano invariati bordo brusco, scatter, range e percorso microfono. Dopo la
conferma il pettine vecchio è escluso finché il fit da otto battiti non è stato
ricostruito: senza questa quarantena 120→160 veniva riconosciuto e poi ributtato
a 53.3 BPM dal pettine ancora fermo sul tempo precedente.

| salto | prima | dopo |
|---|---:|---:|
| 120→150 | 12.8 s | **1.2 s** |
| 120→160 | 23.6 s | **1.1 s** |
| 100→160 | 10.1 s | **1.1 s** |
| 160→100 | 12.0 s | **1.8 s** |
| 120→90 | 10.7 s | **2.0 s** |
| 90→120 | 20.7 s | **2.2 s** |

`probe_tempo_step` ora fallisce oltre 2.5 s o oltre 1 BPM. `probe_matrix
--quick` è identico byte per byte a HEAD: 7.37 s, 18 uscite, 13.37% fuori.
`VPTests --tempo-step` 9/0; `VPTests --tempo-slow` 10/0; `VPTests --bar` 10/0.
Su 300 s × 10 semi:
120 BPM 0 uscite e 160 BPM 0.1% fuori.

I salti 120↔60 e 140↔75 restano fuori apposta: sono relazioni quasi/esattamente
d'ottava e l'audio non può dire se è cambiato il BPM o la suddivisione. Lì la
risposta deterministica resta TAP o ÷2/×2. Il ritardo delle derive continue è
separato: due predittori provati in questa sessione hanno peggiorato il banco
materiali o la fase e sono stati rimossi; non dichiararlo risolto da questo fix.

La serata completa `Flamingo Marco 09.07.26.m4a` (97 minuti, mandata mixer) è
stata campionata in cinque centri-brano riproducibili con
`scripts/analysis/extract_live.swift`. Solo due curve tempogramma erano abbastanza
affidabili come riferimento (ritardo medio 5.12 s), quindi non sono verità
annotata. Sul centro di Sally la catena finale non ha fatto restart aggiuntivi e
ha chiuso a 102.51 BPM, ma `prec.py` misura ancora 94 ms di spostamento mediano
fra finestre e 258.4 ms nel caso peggiore: conferma che la deriva graduale resta
un problema distinto.

### 19. Salto di tempo: il decoder resta incastrato sul vecchio BPM (2026-09-04, SUPERATO dall'item 39 per i salti non d'ottava su linea)

Stato attuale: l'item 39 chiude il blocco per la mandata diretta; questa sezione
resta come cronologia della causa e dei tentativi precedenti. Restano aperte le
relazioni d'ottava e la validazione su microfono/stanza.

Segnalato dall'utente: *«se salto da una velocità all'altra sembra che non trovi
a volte il tempo. Lo ritrova solo se muovo il volume del mic alzandolo o
abbassandolo. Come se a volte uscisse e avesse difficoltà a rientrare nel tempo
se non dopo numerose battute.»*

**Riprodotto e quantificato** con `scripts/probe_tempo_step.cpp` (decoder puro,
niente rete e niente real-time; la riga per compilarlo è in cima al file). Ogni
salto è misurato due volte: normale, e con `notifyInputRestart()` al cambio —
che è esattamente ciò che muovere il gain del mic finisce per provocare, via il
gradino di livello che fa scattare `analysisEpoch`.


| salto         | delta | normale                | con input-restart |
| ------------- | ----- | ---------------------- | ----------------- |
| 120 → 132     | +10%  | 4.1 s                  | 0.5 s             |
| 120 → 150     | +25%  | 17.2 s                 | 0.8 s             |
| **120 → 160** | +33%  | **MAI**                | 0.8 s             |
| 100 → 160     | +60%  | 27.8 s                 | 0.8 s             |
| 160 → 100     | −38%  | 15.6 s                 | 0.6 s             |
| 120 → 90      | −25%  | 10.7 s                 | 0.7 s             |
| 90 → 120      | +33%  | 34.7 s                 | 24.7 s            |
| **140 → 75**  | −46%  | **MAI** (deriva a 150) | 0.8 s             |
| 75 → 140      | +87%  | 8.1 s                  | 0.8 s             |


«MAI» non è un'iperbole né lentezza: su un orizzonte di **182 secondi** dopo il
cambio, 120→160 continua a riportare `bpm=120.00` con `confidence=1.00`. È
certo, ed è sbagliato. La colonna di destra è il workaround dell'utente,
misurato: quasi tutto scende a 0.5–1 s.

**Causa.** Tre cancelli, tutti sulla stessa costante `kOctaveThreshold = 0.25`
(log2, cioè **±19%**) in `Source/AI/BeatDecoder.cpp`:

1. `transitionCandidateAllowed` (~riga 1172) — la strada *rapida* rifiuta un
  candidato oltre ±19% dal `bpm` commesso: `metricalConflict`. Prima ancora,
   `kTransitionMaxRelativeDelta = 0.25` rifiuta oltre ±25%: `outsideRange`.
2. `pullTowardsComb` (~riga 2028) — la strada *lenta e continua* si tira
  indietro esattamente sopra la stessa soglia («a different level entirely —
   not this path's business»).
3. `combDisagrees` (~riga 1631) — questo dovrebbe essere il portello di
  sicurezza (scatta solo *sopra* la soglia), ma sui casi «MAI» non arriva mai a
   far riancorare.

Il commento di `transitionCandidateAllowed` dice che i salti fuori range sono
«affare della macchineria dell'ottava, che ha tutto il buffer». Ma quella si
muove solo agli estremi (`kOctaveTooFast = 168`, `kOctaveTooSlow = 49`) o su un
hint metrico. **Un salto di media portata come 120→160 non è né un'ottava né un
estremo: cade in un buco che non è di nessuno.**

`notifyInputRestart()` funziona perché azzera tutto — `established`,
`intervalAcquired`, la griglia, lo storico dei fit — e rientra in
`TempoRegime::unknown`. Il suo stesso commento lo dice: *«A tempo called fixed
on a room is the most expensive thing to keep: it is designed to be stubborn.»*
La testardaggine è voluta (difesa dal rumore di stanza), ma **non c'è una via di
mezzo proporzionata**: o difende 120 per sempre, o cancella tutto.

**Perché non è mai stato visto:** l'unico test di gradino è **120→132, +10%**
(`"microphone/line confirms rising tempo step within two beats"`), dentro la
finestra. Nessun test supera il ±19%.

- [x] Riprodotto, quantificato, causa isolata (2026-09-04). Probe in `scripts/probe_tempo_step.cpp`.

**Causa esatta trovata (2026-09-04, seconda indagine).** Strumentando il decoder
sul caso 120→160: il comb riporta **106,67**, non 160. `log2(120/106,67) = 0,170`,
**sotto** `kOctaveThreshold = 0,25` — quindi `combDisagrees` è falso e il portello
di sicurezza non scatta mai. 106,67 BPM è un periodo di 0,5625 s, cioè **1,5 volte**
quello vero: il comb sta descrivendo una griglia che rifiuta la maggior parte dei
battiti di cui dovrebbe essere fatta.

È **la stessa trappola già documentata per la fase** nel commento di
`checkGridPhase` (`BeatDecoder.cpp`, ~riga 982): *«una griglia ancorata in levare
non è solo sbagliata, è stabile: ogni battito vero le cade fuori e viene rifiutato
come suddivisione, quelli che restano la fittano puliti… e non c'era niente nella
catena che potesse accorgersene.»* Per la fase l'hanno risolta col fold, che sta
fuori dal gate. **Per il tempo la stessa trappola è rimasta aperta**, e in quello
stato `residual`, `coverage` e `salience` sono tutti sani: il comb è l'unico che
sa, e sta all'11%, sotto ogni soglia.

**Tentativo di fix fatto e RIPORTATO INDIETRO (stessa giornata).** Watchdog
"stale grid": soglia più bassa del salto d'ottava ma voto molto più lungo
(`kStaleGridThreshold` in log2, `kStaleGridVoteBeats` battiti di disaccordo che
regge, con isteresi), e all'attivazione **non** adotta `combBpm` (che è sbagliato
anche lui) ma fa la riacquisizione mirata: molla griglia, fit ed evidenza del
fold, come `notifyInputRestart`.

- **Funziona sul bersaglio:** 120→160 passa da **MAI** a **40,5 s** (con voto 24
battiti; a 48 battiti erano 79 s).
- **Ma regredisce il banco a tempo costante:** a 170 BPM le corse con almeno
un'escursione passano da **4/10 a 10/10**. Verificato che *non* è la taratura
della soglia: con la soglia a 0,24 (appena sotto quella dell'ottava) la
regressione resta identica. È la riacquisizione stessa che costa un transitorio.
- **Perché il primo giro è stato scartato:** il metro usato ("corse con almeno
un'escursione >4%") non distingueva «peggiorato» da «si corregge, e correggersi
costa un transitorio».

**Secondo giro, con il metro giusto — RIUSCITO.** `probe_steady_tempo.cpp` ora
misura **quota di tempo passata fuori** ed **errore medio integrato** su tutti i
frame, non gli eventi. Su quel metro la baseline a 170 BPM è 3,1% di tempo fuori
e 1,93% di errore medio, e il primo watchdog la portava a **6,7% e 3,68%**: era
un peggioramento vero, non il costo del correggersi.

Causa di quel peggioramento, trovata: il watchdog scattava su **qualsiasi**
disaccordo sopra la sua soglia, **relazioni d'ottava incluse**. A 170 alcuni semi
acquisiscono a 85 (rapporto 2:1, `log2 = 1,0`): il watchdog rubava quei casi al
salto d'ottava e li gestiva peggio, riacquisendo a ripetizione.

**Correzione:** confinarlo alla banda che è davvero sua, cioè
`kStaleGridThreshold < apart < kOctaveThreshold`. Sopra quella soglia il
disaccordo è un livello metrico e lo possiede il salto d'ottava, che ha tenure,
salience e vote-hold fatti apposta per quell'argomento.

**Esito misurato** (`kStaleGridThreshold = 0,120`, `kStaleGridVoteBeats = 12`):


| banco                                | baseline                  | con watchdog               |
| ------------------------------------ | ------------------------- | -------------------------- |
| salto 120→160                        | **MAI** (>182 s a 120,00) | **22,6 s**                 |
| resto della tabella salti            | —                         | **invariata**              |
| tempo costante, ogni BPM da 60 a 170 | —                         | **identica alla baseline** |


Cioè: chiude il blocco permanente e **non costa niente** su nessun tempo del
banco a regime. Gli altri «MAI» rimasti (120→60, 140→75) sono relazioni d'ottava,
lasciate al loro path apposta: sono la classe indecidibile dell'item 1.

- [x] **Gate `VPTests` intera (2026-09-04): 610 passed, 7 failed — identico alla
  baseline**, stessi sette test e stesse righe (2 leak + i 5 RED a 50 BPM
  dell'item 1). Nessuna regressione.
- [ ] **Validazione ancora dovuta:** il banco a tempo costante è sintetico. Prima
  di considerarlo chiuso serve la stessa ampiezza di brani/stili con cui è tarato
  il resto del file, e l'**ascolto** — soprattutto: quando il watchdog scatta, la
  riacquisizione si sente come una ripresa o come un buco? Va provata dal vivo
  facendo il salto di tempo che l'ha fatto emergere.
- [ ] Il caso resta lento (22,6 s su 120→160). È una cassaforte, non un
  inseguitore: accorciare ancora il voto è tarare sul banco sintetico. Se serve
  più svelto, la strada è un segnale migliore, non una soglia più bassa.

- [ ] **Decidere il rimedio.** Non toccare `kOctaveThreshold` alla cieca: regge anche la difesa dal rumore di stanza e dalle letture a ottava sbagliata, ed è tarato su misure. La direzione che sembra giusta è una **terza via proporzionata**: evidenza coerente e ripetuta su un tempo fuori soglia per N battute → riacquisizione mirata (quello che oggi fa solo `notifyInputRestart`), senza buttare la fase né la battuta.
- [ ] Test di gradino oltre il ±19% (almeno 120→160 e 140→75), che oggi mancano del tutto.
- [ ] Verificare sul percorso vero (mixer/file con rete), non solo sul decoder: `VPProbe` non ha un `--tempo-step`, va aggiunto.
- [ ] Ascolto.

---

### A. Chiusura ciclo Codex (PATTERN, no loop registrati)

Non serve per suonare oggi in PATTERN; serve prima di commit/review "ciclo chiuso".

- [ ] **Ascolto** render sintetici (VPRender, non loop WAV): `conga-subdivision-dance.wav` e `conga-subdivision-marcha.wav` (es. in `/tmp/`). Densità 1/8 su congas/shaker; niente conga sullo step 0.
- [x] **Patch combinata feature-only**: `.superpowers/sdd/virtualperc-feature-combined.patch`, base `1cb93fc`, 33 file, SHA-256 `d3176210924f0f6246f42d945e595fced33b1a6b95bfa0888ee9e620b6ac6781`. Esclude `Assets/Loops/`, `Source/Loops/`, `HANDOFF_LOOP_DEBUG`, `.agents`, CMake e i lavori adiacenti clap/cembalo; apply/roundtrip verificato byte per byte; CRLF preservati.
- [x] **Report** in `.superpowers/sdd/`: numeri finali e descrizione veto/freeze allineata al codice in `makeup-phase-root-cause.md`, `makeup-phase-fix-report.md` e `phase-156-root-cause.md`.
- [x] **Lint / whitespace** sulla patch combinata: `git apply --check --whitespace=error-all` verde. Il repository non definisce un target lint/tidy/format dedicato; restano sette warning preesistenti in `Source/AI/OnnxSession.cpp`.
- [x] Snapshot pre-commit: `VPTests` due volte (`575/576`, poi `576/576`), `VPAlign` exit 0 con otto righe PASS, `VPTiming` exit 0, `VPRoom` exit 0 e build simulatore riuscita. Nessun ulteriore run va avviato senza approvazione esplicita.

Chiusura tecnica completata il **2026-09-04** senza commit. La chiusura musicale resta sospesa esclusivamente all'ascolto umano dei due WAV sopra; non dichiararla completata prima della conferma.

Artefatti singoli già prodotti (se servono): `rapid-tempo-complete-diff.patch`, `conga-complete-diff.patch`, `sparse-leak-fix-diff.patch`, `makeup-phase-fix-diff.patch`.

### B. Loop registrati (WAV)

Vedi `**docs/HANDOFF_LOOP_DEBUG.md**`. Switch LOOP/PATTERN, banco `Assets/Loops/dance`, debug iPad (gracchiio, 48 kHz, swing oltre 18%, ecc.). Fuori scope finché resti su PATTERN.

---

## Chiuso

*(sposta qui gli item con data breve quando sono integrati)*

---

## Note per chi riprende

- Skill tempo: `.claude/skills/realtime-tempo/SKILL.md`  
- Skill parti: `.claude/skills/percussion-patterns/SKILL.md`  
- Tempo lento ~50 BPM + hat ottavi: item 1 — root cause confermata (ottava bloccata sugli ottavi). Due versioni di `BeatDecoder::observeDownbeatCadence` provate e insufficienti (soglia forte; poi probabilità continua con istogramma a tempo-indicizzato): la curva di downbeat della rete non distingue le due ipotesi su questo materiale per via della stessa ambiguità 1-vs-3 dell'item 2. Non riprendere con un altro tentativo isolato su pBeat/pDownbeat — serve una feature spettrale nuova o una validazione multi-brano estesa (vedi item 1 per i dettagli).  
- Battuta: `BeatTracker::alignBarFromVotes` / `notifyBarReentry`, `maybeDetectBarReentry`, `barLocked` (item 2 chiuso). `VPTests --bar`.
- Cambio brano a START on: `loadInternalTrack` senza reset tracker (item 3)  
- Drift guard: settings + mute in render; non mescolare con `BandDynamics::wantsSilence` (item 4)  
- Suddivisione: STRUMENTI `subAuto` / `sub4` / `sub8` / `sub16`; AUTO deve adattare 1/4↔1/8↔1/16 sull'ottava (item 6)  
- Swing: oggi knob `swingSlider`; deve diventare tasto ON/OFF in STRUMENTI (item 7); warp in `GrooveEngine::humanDelay` (`kFullSwingBeats`)  
- STOP: oggi `VirtualPercussionEngine::stop` + `percussion.silence()` immediato; deve diventare trillo shaker + fade (item 8)  
- PARTE: select custom `StyleSelect` + DINAMICA (`MainComponent`); AUTO prima voce, default motore MARCHA / `grooveAuto` off (item 12)
- UI tasto battuta: tap su «L'1 è QUI» sblocca senza nudge (`barControlNudgeOnTap`, item 13)
- NATURALE: `GrooveEngine::setShakerNatural`, tasto in STRUMENTI, default off (item 11)
- Clap gate (item 10): `tr.barTrusted` dal tracker (lucchetto o istogramma sullo zero, muto in finestra di rientro). Ascolto render + brano OK; taglio/seek coperti da `VPTests --bar`.
- Brano: `trackLoadButton` / `trackPlayButton` / `trackTransport`; waveform+seek in SETUP (`TrackWaveform`, item 14)  
- ÷2 / ×2: **rimossi** (item 15, 2026-09-03); ottava sempre auto in `BeatTracker`  
- Ottava automatica: si muove **solo se non sta suonando nulla** (item 17, `BeatTracker::updateAutoOctave`). Il livello si sceglie in acquisizione e si tiene; ÷2/×2 restano la via manuale anche a parte suonante. Regressione: `VPProbe --trace --live --mixer plain 168` deve finire ~167, non 84.
- Pattern DANCE (2026-09-15): groove classico sempre in levare — core 4 note (2/6/10/14: tapado/open), leggeri slap che variano fra le barre. A≠B≠C≠D=A (frase di 4 bar chiusa) + Fill. Variazione musicale senza perdere il groove classico. **Ascolto ancora da fare.**
- Layout compatto (2026-09-15): il buco sotto i pallini era in `compactGeom` — `tempoH = rest - transportH` dava alla colonna del tempo *tutto* l'avanzo, mentre le sue righe hanno dimensione fissa. Ora la colonna prende la sua altezza naturale (`kCompactTempoNatural`, 236 pt) e l'avanzo scende: START/STOP fino a 132 pt, poi MISURE su due righe, poi FEEL. `compactTempoRows` scala solo verso il basso, mai verso l'alto. «L'1 è QUI» ha una riga propria sotto i pallini ed è visibile anche su telefono (prima era `setVisible(!compact)` e in `layoutCompact` non veniva neanche posizionato). MISURE e FEEL vanno a capo (3+4 quadrati, 3+2 knob) solo quando è la *larghezza* a stringere, così l'iPad resta su una riga. Verificato a video su macOS a 420×880; build iOS device OK.
- Safe area (2026-09-15): due problemi distinti, stessa origine.
  1. `Displays::safeAreaInsets` di JUCE torna a zero. Aggiunta `vp::windowSafeAreaInsets()` (`IosMicPermission.mm`): legge `safeAreaInsets` dalla key window della scena in primo piano; `MainComponent::effectiveSafeArea()` prende il massimo fra quella e il valore JUCE.
  2. **Il bug che si vedeva davvero**: iOS consegna la safe area *dopo* il primo `resized()`, e nessuno rifaceva il layout. I componenti (SETUP, ÷2/×2, «L'1 è QUI») restavano posizionati come se non ci fosse il notch, mentre il `paint()` — che legge gli inset quando gira — disegnava ~50 pt più in basso. Da qui i tre sintomi che sembravano indipendenti: SETUP sotto la status bar, i pulsanti ottava scollati dal numero, il bottone sopra i pallini. Misurato sullo screenshot dell'utente: pill dipinta a ~71 pt, SETUP (componente) a ~22 pt. Fix: `timerCallback` confronta `effectiveSafeArea()` con `laidOutSafeArea` e richiama `resized()` quando cambia — copre anche rotazione e drag di Split View.
  **Da verificare su device**: è l'unico pezzo non provato a video (il simulatore non si avvia su questa macchina: `xcode-select` non punta a Xcode, e il bundle risulta misto device/simulatore dopo due build nella stessa dir).
- Assestamento a volte lentissimo (fino a 30 s) sullo stesso materiale che di solito prende 2 s: item 18, aperto, **non** inseguirlo prima di sapere se esiste fuori dal banco (`VPProbe --sync`).
- Chiusura Codex PATTERN: parte tecnica completata; resta l'ascolto umano in **Standby A**. Loop WAV registrati: **Standby B** + `HANDOFF_LOOP_DEBUG.md`
- Guadagno automatico analisi (item 16): `kMakeupClipGuardPeak` in `VirtualPercussionEngine.cpp`, attenua solo sopra 0.90 di picco. Test veloce dedicato: `VPTests --octave` (non lanciare la suite intera per iterare qui). Full-suite gate e ascolto ancora da fare.
- Item 60 (2026-09-29): regressione item 59 (`probe_tempo_step` 120→160 → 53.3) chiusa escludendo i sottomultipli in `combOtherSlower`; griglia mai confermata + affamata + parte che suona: il pettine corregge senza aspettare `levelSettled`. Banco: solo LET ME LOVE YOU 44k cambia (aggancio 27.9 → 20.3 s). Lanciare `bench_songs` senza altri processi pesanti: sotto carico non è deterministico.
- Item 61 (2026-09-29): primo quarto — sul conteggio fidato un quarto si sposta solo se rete e armonia concordano (`BeatTracker::tryAlignFrom`); fuori dall'1 1370 → 914 s sul banco. Anticipare la decisione d'ingresso è peggio (4 giuste / 6 sbagliate); il veto armonico sulla mezza battuta è respinto.
- Item 62 (2026-09-29): 1000 GIORNI — pickup irregolare all'inizio (147 invece di 161 per ~12 s) e brano sul bordo d'ottava 81/162; ÷2 lo segue bene.
- Item 63 (2026-09-29): ÷2 manuale pubblicato dal decoder naturale (`setUserOctave (n, manual)`); AUTO e ×2 invariati. Scatti a ÷2 1.08 → 0.76; percorso naturale byte per byte uguale a HEAD.
- Item 64 (2026-09-29): non regressione — tutto verde tranne `--phase-lock` 156 → 78, introdotto da `4389cca` (item 57, regola d'ottava); test adeguato alla regola (fase sul livello tenuto), 20/0 con margine stretto sul 156 (-7.3 ms su 8).
- Item 65 (2026-09-30): fedeltà colpi/batteria — misura nuova `onset_fit.py` (3.48 uscite/min, 8.9% oltre 25 ms; griglia del decoder 1.84/min); C1 (fiducia di fase), C2 (trim più rapido), D1 (ancora FISSO più rapida) ed E1 (cassa dal mix datata al campione) respinti: lo scarto medio resta 11.5 ms, il clock è a ~3 ms dalla griglia lisciata del decoder. Motore invariato; serve l'ascolto dei punti indicati.
- Item 66 (2026-09-30): scatti di velocità — in direct-live la piega piena (7.5%) solo con moto provato dal decoder, altrimenti 3% fino a 0.15 battiti; scatti oltre il 3% 91 → 62 sul banco, scarto dalla batteria invariato; costo sulle rampe sintetiche grandi (continuo product-direct 45.7 → 49.7 ms).
- Item 67 (2026-09-30): trim che spinge contro la fase dimezzato a ogni osservazione (T2): dopo un colpo spostato il clock rientra in 3 battiti invece di 8 s; scarto dalla batteria 11.50 → 11.33 ms. Silenzio/STOP automatico: i segnali attuali sbagliano 9 volte su 10 e il caso "incastrato" non è riprodotto; serve un caso vero.
- Item 68 (2026-09-30): in FISSO stabile (8 s sulla stessa griglia) un obiettivo di fase lontano deve tenere il lato per due battiti prima di essere adottato o di aprire il tetto di sterzo; EVERYTIME 48k 1:53 da 104–132 a 117–127 BPM, resto del banco invariato. H1 (stessa idea in VIVO oltre 0.15 battiti) respinto.
- Item 69 (2026-09-30): scatti scomposti per origine (`surge_sources.py`); i picchi grandi sono gradini falsi confermati dal decoder. Su ingresso diretto il clock salta solo se la confidenza della transizione è ≥ 0.75 (i gradini veri leggono 0.89–1.00): scatti% 0.83 → 0.79, INFINITO senza più scatti, `VPAlign --steps` identico.
- Item 70 (2026-09-30): dal vivo solo mix completo. Agganciare i battiti all'attacco più vicino nel mix li rende più irregolari (5 brani su 6): respinto prima di toccare il motore. Aggiunto il registro degli STOP/START (`Documents/VirtualPercussionist-stopstart.log`, visibile in File) con l'entità del riallineamento; serve una prova dell'utente.
- Item 89 (2026-10-05): pulizia del codice inutilizzato, banco identico bit per bit.
- Item 88 (2026-10-05): dal vivo mute con band già in corso — la mandata ha poco basso (`rhythmSeen`) e il livello era giudicato prima del guadagno; ora un aggancio sicuro di 4 s su ingresso diretto basta.
- Item 87 (2026-10-02): verità dal maestro offline (Beat This!, `truth.py`). Il tetto non è BeatNet: picchi a 7 ms, retta causale 10, griglia del decoder 12, clock 17 ms (32% oltre 25 ms). Il margine è fra griglia e clock. Tenuta la fase dalla retta su 6 battiti: 21.6 → 17.9 ms, scatti 1.18 → 0.78/min; `--phase-lock` 156 a ÷2 fuori di 1.1 ms, da decidere. Uno: la sola rete corregge un quarto con distacco ≥ 0.30, 50.6 → 57.5%; oltre serve una rete migliore (fase 2). Fase 2: BeatNet rifinito sulle etichette del maestro (`train_beatnet_finetune.py`, 1000 passi, KD 3) è ora il modello dell'app: 13.0 ms, >25 ms 22.2%, livello giusto 71.9%, uno 67.3%; ottava ancora fragile.
- Item 71 (2026-09-30): pesi Ballroom e Rock Corpus di BeatNet provati sul banco: peggiori di GTZAN, respinti. Registro STOP/START dell'utente: la griglia non è spostata, l'app corre 1–2% sopra il pettine per ~8 s (riprodotto sul Flamingo 64:19, confermato dagli attacchi). Sei varianti di "più autorità al pettine" respinte: o rompono le rampe o peggiorano altri brani.
# Priorità recupero diretto — 09/09/2026

Checkpoint credito limitato: rifinitura iniziale a due intervalli concordanti
mantenuta su linea; INFINITO centrale +2 s 95.43 -> 92.27 BPM, deriva media
17.5 -> 9.0 ms. Banco ridotto: stabilità sostanzialmente invariata, aggancio medio
7.28 -> 7.30 s (non miglioramento universale). Verifica nell'app/mixer ancora aperta.

Input brano/mixer prima del microfono esterno. Correzione dopo conferma accelerata
e verificata con 84 casi mirati; riconoscimento tardivo dei cambi BPM e residui
lenti ancora aperti. Misure, comandi e prossima azione in `HANDOFF_TEMPO.md`,
sezione «Follow-up recupero — 09/09/2026». Non dichiarare risolti tutti i deragliamenti.
