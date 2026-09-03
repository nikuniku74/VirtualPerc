# TODO integrazione

Spunta `- [x]` quando un pezzo è integrato e verificato.  
Se scopri qualcosa di nuovo, aggiungi una riga. Se un item cambia, aggiorna il testo, non cancellarlo in silenzio.

**Ordine:** 1 = più importante (senza questo il resto non tiene). Non riordinare per recenza.

**Fuori da questo elenco (standby):** loop registrati (`docs/HANDOFF_LOOP_DEBUG.md`); chiusura formale ciclo Codex PATTERN — sezione **Standby** in fondo. Mic iPad / speaker in stanza: non è il path di verifica (si testa **brano caricato** e **mixer**).

**Come verificare:** per DSP/tempo, un numero di probe o un test; per musica, ascolto. Non dichiarare chiuso un item musicale senza orecchio.

---

## In corso / da fare

### 1. Tempo lento (~50 BPM): cassa / rullo / charleston a ottavi

A tempi lenti (es. **50 BPM**) il groove da batteria è chiaro: **cassa sull'1 e sul 3**, **rullo sul 2 e sul 4**, **charleston (hi-hat) a ottavi**. L'app **non tiene bene il tempo**, anche se il charleston dà un pulse ogni ottavo — non è un brano "vuoto".

Sospetti (skill tempo: sotto ~100 BPM il fold legge gli ottavi come beat; range 40–220; PLL con quarto da 1,2 s): ottava 50 vs 100, fase che deriva, lock che non si chiude. Non "sistemare" bloccando sempre su 100. Il clock non riparte perché il BPM è basso.

- [ ] Riprodurre su **brano caricato** e mixer: 50 BPM, cassa 1/3, rullo 2/4, hat ottavi. Annotare BPM tenuto, deriva di fase (ms), ottava (50 o 100), stato lock. Stesso brano a ~100 come controllo.
- [ ] Capire se sbaglia il **livello** (ottava) o il **seguire** (PLL / decoder / kick). Gli hat a ottavi devono contare, non solo la cassa.
- [ ] Fix + probe (`VPAlign` o equivalente a 50) e ascolto. Margini: ±1 BPM tenuto, fase udibile sul quarto, niente slittamento su 20 s.
- [ ] Docs in `.claude/skills/realtime-tempo/SKILL.md` (caso lento + backbeat + hat).
- [ ] Gate: `VPTests` invariato sui tempi già coperti; niente regressione a 100–156.

---



### 2. Capire qual è il primo quarto (e riallinearlo dopo un taglio)

L'app deve mettere l'**1** sul primo quarto del 4/4 su mixer e brano caricato, senza «L'1 è QUI». Se il musicista **taglia due quarti** e riprende in 4/4, deve riallineare l'1 in poche battute. Niente look-ahead; il clock del tempo non riparte; ruotare l'1 = solo `rotateBarIndex`.

Oggi: istogramma downbeat + armonia (`BeatTracker::alignBarFromVotes`). In play servono ~8 battute prima di ruotare. `barLocked` (tasto acceso) **vieta** ogni rotazione automatica. Sul line/mixer l'1 tenuto è circa 20–21/25; 1 vs 3 resta l'ambiguità.

- [ ] **Baseline:** bench 4/4 line/file, lucchetto spento. Annotare quarto all'ingresso, tenuto 20 s, e stesso brano con buco di 2 quarti. Scrivere i numeri (oggi: lento o mai).
- [ ] **Test RED taglio:** lock sull'1 → tace 2 quarti → riparte sull'1 musicale (per l'app è il 3). Entro 2 battute dal rientro `beatInBar` è di nuovo 0. Fill senza buco di livello **non** ruota. Con lucchetto **non** ruota. Tempo/decoder non restartano.
- [ ] **Finestra di rientro:** dopo "la band riparte" (livello/epoch, **senza** `notifyInputRestart` da taglio), 2–4 battute con allineamento tipo `comingIn`. Una rotazione per rientro. RT: niente alloc/lock/I/O.
- [ ] **Ingresso file/mixer:** `VPBar` (o equivalente) su `internalPlayer` / `kitMic`. Se il file è peggio del line feed, è un bug di path, non si allentano i margini.
- [ ] **Docs:** `.claude/skills/realtime-tempo/SKILL.md` + `docs/AUDIO_ENGINE.md` (mixer/file vs stanza; rientro; lucchetto).
- [ ] **Gate:** `VPTests` + `VPAlign` invariato; ascolto su brano caricato e, se possibile, mixer. Il clap (item 10) **dipende da questo**: senza 1 vero, "1 e 3" in app è "2 e 4" in sala.

Piano discusso: Task 0 = baseline; 1 = RED; 2 = finestra rientro; 3 = path file; 4 = docs.

---



### 3. Cambio brano a START già on (60 → 120 non riallinea)

Se carico un brano a es. **60 BPM**, premo **START**, poi carico **un altro** a **120 BPM**, l'app **non si allinea subito**. Se prima premo **STOP**, al nuovo brano **si allinea subito**.

Oggi `loadInternalTrack` sostituisce il file e fa `trackTransport.start()`, ma **non** resetta il tracker (`engine.stop()` / `notifyInputRestart`). Resta il lock del 60 sul pezzo a 120.

Un file nuovo è un **ingresso nuovo**, non una deriva del brano precedente. L'utente non deve dover premere STOP. START può restare acceso; BPM e fase devono chiudersi sul B con la stessa prontezza di uno START pulito. Il clock non "riparte" a caso a ogni BPM: è il cambio di sorgente che va dichiarato.

- [ ] Riprodurre: START su file A (60) → CARICA file B (120), senza STOP. Oggi: tempo/fase del A. Con STOP in mezzo: lock rapido sul 120.
- [ ] Al load (e analogo seek, item 14): nuovo ingresso (restart decoder / finestra coming-in), non continuare il PLL del A. RT: niente alloc.
- [ ] Percussioni sul B in poche battute, non restare a 60 per 8–20 s.
- [ ] Test: A 60 → B 120 e A 120 → B 60, senza STOP; BPM e fase del secondo file. Ascolto.
- [ ] Docs skill tempo: cambio file ≠ taglio in-song (item 2).

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



### 5. Cambio parte (style) → esce dal tempo

Quando si cambia lo stile delle percussioni, a volte **perde il tempo**.

- [ ] Riprodurre: quale style, a che BPM, START già on, su che fonte (file/mixer).
- [ ] Tracciare: cambio al quarto (`PercussionEngine`), `alignPhrase`, voices, retrigger, clock. Il tempo non deve muoversi; sospetti: phrase/bar, attacco, buco di eventi.
- [ ] Fix minimo + test (cambio style a tempo locked, fase continua, niente pulse skip).
- [ ] Ascolto del cambio in play.

---



### 6. Pulsante AUTO (STRUMENTI) — densità che segue l'ottava del tempo

In **STRUMENTI**, **AUTO** oggi è solo un alias di **1/8**: non ascolta il brano e non cambia mai griglia. Deve diventare la modalità che fa quello chiesto sul raddoppio/dimezzamento.

**Manuale (fisso):** `1/4`, `1/8`, `1/16` restano quello che premi. Non si auto-adattano. Se il BPM raddoppia con 1/16 acceso, la parte resta a sedicesimi (e suona più veloce): è una scelta esplicita.

**AUTO:** tiene la **densità percepita** quando il tempo **raddoppia o dimezza** (ottava metrica). Copre tutte e tre le griglie, spostandosi tra loro. Il clock resta a sedicesimi; si cambia solo il thinning in `GrooveEngine`. Niente restart del clock, niente look-ahead.

Tabella (ottava 0 = lock "giusto", densità di riposo = **ottavi**, come oggi):

| Ottava del tempo | Griglia che suona |
|---|---|
| −1 (BPM circa **metà**) | **1/16** |
| 0 | **1/8** |
| +1 (BPM circa **doppio**) | **1/4** |

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



### 7. Swing: pulsante ON/OFF in STRUMENTI (niente knob)

Oggi lo swing è una **knob** 0–100% (`swingSlider` → `settings().swing` 0..1). Non serve una quantità: o è **dritto** o è **swing**. Togliere la knob; in **STRUMENTI** (accanto a shaker / congas / AUTO / 1/4 / 1/8 / 1/16) un pulsante **SWING** acceso/spento.

**ON** = swing pieno (terzina): l'"&" sta a **due terzi** del quarto (`kFullSwingBeats = 1/6`, `humanDelay` in `GrooveEngine`). **OFF** = 0, griglia dritta. Nessun valore intermedio in UI. Cambio come già per parte/swing: **al prossimo quarto**, non a metà beat.

- [ ] UI: pulsante in STRUMENTI; knob `swingSlider` / label / prefs come fader **via**. Prefs: bool (o 0/1). Default **off**.
- [ ] DSP: ON scrive `swing = 1`, OFF `swing = 0`. Non cambiare la formula del warp; se è sbagliata, sistemarla qui.
- [ ] **Verificare che lo swing sia fatto per davvero**, non solo "acceso":
  - quarti (step 0/4/8/12) **non** ritardati dallo swing;
  - l'"&" (step 2/6/10/14) a swing pieno atterra a **2/3 del quarto** (un sesto di beat tardi), non "un po' dopo";
  - "e" e "a" (step dispari) **seguono lo stesso stretch**, così a 1/16 la parte shuffle e non combatte gli ottavi ritardati;
  - `delayBeats` sempre ≥ 0 (mai anticipo);
  - humanize resta un jitter a parte, non si confonde con lo swing;
  - commit al prossimo quarto (test già in `TestMain`).
- [ ] Misura: test di timing (onset vs griglia / `VPTiming` o equivalente) + render `VPRender --swing 1 --click` e ascolto. Se i numeri non coincidono con 2/3, è un bug, non "feel".
- [ ] Loop registrati: restano standby; se il path loop rifiuta swing alto (`swing massimo 18%`), con il tasto ON restare sul path sintetico o documentarlo — non silenziare in silenzio.
- [ ] Docs skill percussioni: controllo = tasto, non knob.

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



### 9. Volumi separati shaker e congas

Oggi c'è **una knob** per le percussioni. Servono **due volumi** (shaker / congas), indipendenti.

- [ ] Settings + UI (due controlli; default musicali da scegliere).
- [ ] DSP: guadagni per voce in render, audio thread safe.
- [ ] Test: uno a zero silenzia solo quella famiglia; l'altro resta.
- [ ] CLAP e CEMBALO (item 10) avranno ciascuno un volume proprio: non unire tutto in una knob.

---



### 10. CLAP e CEMBALO oltre shaker e congas

Due voci in più, ognuna con **enable** e **volume proprio** (vedi item 9). Stesso clock a 16th.

**CEMBALO** (piatto). Non è un pattern a parte: è **lo stesso mestiere dello shaker**, con un altro suono. Stessa griglia, stesso thinning (AUTO / 1/4 / 1/8 / 1/16, item 6), stesso swing, stessi accenti/pesi per stile, stesso humanize/dinamica. Acceso, suona sugli stessi step dello shaker; spento, tace. Shaker e cembalo possono stare entrambi on (due timbri sulla stessa parte) o uno solo. L'invariante "niente conga sul uno" **non** riguarda il cembalo: come lo shaker, può (e deve) cadere sul pulse.

**CLAP.** Non segue lo shaker. Suona **solo sul rullo**: in 4/4 i quarti **2 e 4** (backbeat), non ogni step.

**Allineamento (obbligatorio, item 2):** quei quarti devono essere l'1/2/3/4 **di quello che l'app sta sentendo**, non del conto interno se è ruotato. Se `beatInBar` è sfasato di due quarti, il clap "sull'1 e sul 3" dell'app in sala sta sul **2 e sul 4** (e il clap "sul rullo" sta sul 1 e sul 3). Non accettabile. Il clap **non parte** finché la battuta non è fidata (stesso criterio dell'item 2: 1 tenuto, non 1 vs 3 ambiguo, riallineato dopo un taglio di due quarti). Non "sistemare" il pattern a orecchio (mettere 2 e 4 per compensare un 1 sbagliato). Con lucchetto acceso vale il conto dell'utente.

- [ ] Articolazioni in banca (o sintesi fallback) + `Stroke` per clap e per cembalo.
- [ ] Enable + volume per ciascuno.
- [ ] Cembalo: riusare la logica/eventi shaker (stessi step, stessa suddivisione); solo sample/voce diversi.
- [ ] Clap: solo step del rullo (default 2 e 4); silenzio altrove.
- [ ] Clap gated sulla battuta fidata; con 1 ruotato di 2 quarti il clap **non** deve restare sul posto sbagliato (tace o ruota con l'1, come l'item 2).
- [ ] Test: cembalo segue 1/4 vs 1/16 come lo shaker; clap sul rullo del **brano**, non del bar index storto; ognuno mutabile da solo.
- [ ] Ascolto su file/mixer: clap coincidente col rullo vero; dopo un taglio di due quarti, o tace o torna al posto giusto.
- [ ] Docs in `.claude/skills/percussion-patterns/SKILL.md`.

---



### 11. Shaker più naturale (non griglia fissa)

Oltre a 1/4, 1/8, 1/16 fissi: in **ottavi**, ogni tanto qualche **sedicesimo** (e analoghi sulle altre griglie, se ha senso). Thinning che **aggiunge** eccezioni, non solo toglie.

- [ ] Spec musicale: probabilità, su quali step (e/a), mai sul 1 delle congas; quanto spesso a 1/8 vs 1/4.
- [ ] Implementare in `GrooveEngine` (RNG deterministico, seed come il resto).
- [ ] Opzione dedicata (non rubare il tasto 1/8). Nome da decidere: es. «NATURALE» / «FEEL».
- [ ] Test: 1/8 puro vs naturale (ci sono odd stroke; non diventa un 1/16 pieno).
- [ ] Ascolto.

---



### 12. PARTE: dropdown stili (fino a DUE-UNO) + DINAMICA accanto

Oggi PARTE è una **riga di quadrati**: AUTO, MARCHA, ROCK, DANCE, POP, SAMBA, FUNK, REGGAE, BOSSA, DUE-UNO, e **DINAMICA** in fondo (`placeSquareRow` in `MainComponent.cpp`). Troppi tasti.

**Layout:** un **select custom** (dropdown, non ComboBox nativo iOS/macOS che rompe il look) con tutti gli stili **fino a DUE-UNO**, e **accanto** il pulsante **DINAMICA così com'è** (toggle `dynamicsFollow`, stesso comportamento). Niente altri controlli in quella riga.

Voci del select, in quest'ordine: **AUTO**, MARCHA, ROCK, DANCE, POP, SAMBA, FUNK, REGGAE, BOSSA, DUE-UNO. Default: **AUTO** selezionato (`grooveAuto` on, come oggi). Scegliere uno stile spegne AUTO (`applyStyle`). Rimettere AUTO riaccende il detector. DUE-UNO resta solo manuale (il detector non lo sceglie mai). Non confondere con AUTO in STRUMENTI (item 6).

**Stile e altezza:** select chiuso e tasto DINAMICA **stessa altezza**, stesso chrome (fill, bordo, tipo, colore acceso/spento come gli altri `TextButton` di PARTE/STRUMENTI). Il menu aperto stesso linguaggio visivo, non un picker di sistema. Con AUTO acceso, il chiuso mostra **AUTO** (si può accennare lo stile rilevato senza selezionarlo — oggi i tasti si tintano).

- [ ] Sostituire i dieci `style*` button con un controllo custom; tenere `dynamicsButton`.
- [ ] Default AUTO; prefs: stessa semantica `grooveAuto` / `grooveStyle`.
- [ ] Altezze allineate + stesso stile (iPad e Designed for iPad).
- [ ] DSP invariato: cambio stile ancora al prossimo quarto (item 5 resta un bug a parte).
- [ ] Touch: area del select usabile; menu non copre START/STOP in modo permanente.

---



### 13. Tasto «L'1 è QUI» / «SPOSTA L'1»

**Comportamento attuale (non un bug di stato):** è **lo stesso pulsante**.  
«SPOSTA L'1» = sbloccato, un click **sposta l'1 di un quarto e blocca**. Accesso «L'1 è QUI» = *il conto è tuo, l'auto non lo tocca*. Quattro spostamenti = un giro di battuta; il **quinto click** sblocca e torna «SPOSTA L'1». Anche un TAP che dichiara l'1 accende il lucchetto.

Su mixer/file il lucchetto **impedisce** il riallineamento automatico (item 2). In palco, di default conviene **sbloccato**.

- [ ] Decisione UX (scriverla qui):  
  ```
  A) lasciare 5-click per sbloccare, magari con hint in UI;  
  B) secondo gesto per sbloccare (press lungo / tap sul tasto acceso senza nudge);  
  C) altro.
  ```
- [ ] Implementare la decisione.
- [ ] Test del lucchetto (già esiste `bar-lock`) allineati al nuovo gesto.
- [ ] Copy in UI così non sembra un tasto "rimasto acceso per sbaglio".

---



### 14. Waveform del brano caricato (seek in impostazioni)

Quando un brano è caricato (`internalPlayer`, `trackTransport`), in **impostazioni**, **sotto** i pulsanti CARICA / PLAY (riga INGRESSO: `sourceButton`, `trackLoadButton`, `trackPlayButton`, …) c'è la **classica onda** da inizio a fine.

Trascinare il dito lungo l'onda = anteprima della posizione. **Rilascio** = riproduce **da quel punto** (`trackTransport.setPosition` + play). Non seek continuo mentre si tiene premuto (niente scratch): il salto è al sollevare il dito.

Senza brano: l'onda non c'è (o è vuota/disabilitata). Playhead che segue il brano mentre gira. Stesso look della pagina SETUP (non un widget iOS nativo).

Il salto è un taglio per il tracker: non lasciare le percussioni sul vecchio punto del brano. Rientro come item 2 (finestra coming-in / epoch), senza restartare il clock del tempo. Cambio di **file** (altro brano) è l'item 3, non questo.

- [x] Peaks dell'onda calcolati al load (fuori dall'audio thread); ridisegnare al resize (`buildTrackWaveform`, 1024 colonne).
- [x] Visibile solo con file caricato, sotto CARICA/PLAY; layout SETUP che cresce (non coprire CLOCK/BUFFER).
- [x] Drag + release → seek + play da lì; playhead in play (`TrackWaveform` + `seekInternalTrack`).
- [ ] Tracker/perc: dopo il seek, l'1 e la parte si riallineano al nuovo punto (item 2); test file + ascolto. Seek chiama `engine.notifyTrackSeek()` (epoch); finestra coming-in completa = item 2.
- [x] Touch: un dito, niente zoom obbligatorio; brani lunghi restano una barra sola inizio→fine.

---



### 15. Pulsanti ÷2 e ×2: servono o si tolgono

**Decisione (2026-09-03):** rimossi. Con `tempoOctaveAuto` sempre attivo,
`BeatTracker::updateAutoOctave` tiene il livello metrico; i pulsanti forzavano
solo un override manuale che duplicava confusione. Casi ambigui (es. 50 BPM,
item 1) vanno risolti nel tracker, non con un workaround in UI.

- [x] Verificare ascoltando: con AUTO ottava, ÷2/×2 cambiano qualcosa di utile o solo confondono.
- [x] Se inutili: togliere i due pulsanti, layout, prefs `tempoOctave` da UI, `applyTempoOctave` / click handler. Lasciare `tempoOctaveAuto` e il path automatico in `BeatTracker` se ancora usati dal decoder.
- [x] Se si toglie anche `setTempoOctave` utente: test esistenti su ottava manuale da aggiornare o cancellare **solo** quelli del override; non allentare i test di lock/BPM. (`setUserOctave` nel decoder resta per i test `octave-control`.)
- [ ] Nessun regressione su START, TAP, suddivisione, «SPOSTA L'1» — gate: `./scripts/run-tests.sh` (ultima run: 575 pass, 1 fail `leak-138` ONNX flaky, non legato a questo item).

---



## Standby

Lavoro **non bloccante** se usi solo **PATTERN** (motore sintetico / `GrooveEngine`, switch LOOP spento). Il codice del ciclo Codex (tempo rapido, suddivisione congas, canceller, epoch/make-up, 156 BPM, test) è già nel tree; qui resta la **chiusura formale** e l'integrazione **loop registrati** (altro documento).

### A. Chiusura ciclo Codex (PATTERN, no loop registrati)

Non serve per suonare oggi in PATTERN; serve prima di commit/review "ciclo chiuso".

- [ ] **Ascolto** render sintetici (VPRender, non loop WAV): `conga-subdivision-dance.wav` e `conga-subdivision-marcha.wav` (es. in `/tmp/`). Densità 1/8 su congas/shaker; niente conga sullo step 0.
- [ ] **Patch combinata feature-only**: esclude `Assets/Loops/` e path loop; apply/roundtrip verificato; CRLF preservati.
- [ ] **Report** in `.superpowers/sdd/`: numeri finali, testi obsoleti rimossi (`makeup-phase-fix-report.md`, `progress.md`, ecc.).
- [ ] **Lint / whitespace** sulla patch combinata.
- [ ] (Opzionale) secondo giro `VPTests` + `VPAlign` + build simulatore come snapshot pre-commit.

Artefatti singoli già prodotti (se servono): `rapid-tempo-complete-diff.patch`, `conga-complete-diff.patch`, `sparse-leak-fix-diff.patch`, `makeup-phase-fix-diff.patch`.

### B. Loop registrati (WAV)

Vedi **`docs/HANDOFF_LOOP_DEBUG.md`**. Switch LOOP/PATTERN, banco `Assets/Loops/dance`, debug iPad (gracchiio, 48 kHz, swing oltre 18%, ecc.). Fuori scope finché resti su PATTERN.

---



## Chiuso

*(sposta qui gli item con data breve quando sono integrati)*

---



## Note per chi riprende

- Skill tempo: `.claude/skills/realtime-tempo/SKILL.md`  
- Skill parti: `.claude/skills/percussion-patterns/SKILL.md`  
- Tempo lento ~50 BPM + hat ottavi: item 1  
- Battuta: `BeatTracker::alignBarFromVotes`, `nudgeBar`, `barLocked` (item 2)  
- Cambio brano a START on: `loadInternalTrack` senza reset tracker (item 3)  
- Drift guard: settings + mute in render; non mescolare con `BandDynamics::wantsSilence` (item 4)  
- Suddivisione: STRUMENTI `subAuto` / `sub4` / `sub8` / `sub16`; AUTO deve adattare 1/4↔1/8↔1/16 sull'ottava (item 6)  
- Swing: oggi knob `swingSlider`; deve diventare tasto ON/OFF in STRUMENTI (item 7); warp in `GrooveEngine::humanDelay` (`kFullSwingBeats`)  
- STOP: oggi `VirtualPercussionEngine::stop` + `percussion.silence()` immediato; deve diventare trillo shaker + fade (item 8)  
- PARTE: oggi riga di quadrati `styleAuto`…`styleTwoOne` + `dynamicsButton`; deve diventare select custom + DINAMICA (item 12)  
- UI tasto battuta: `MainComponent.cpp` (`barButton`, `barTapsSinceLock`) (item 13)  
- Brano: `trackLoadButton` / `trackPlayButton` / `trackTransport`; waveform+seek in SETUP (`TrackWaveform`, item 14)  
- ÷2 / ×2: **rimossi** (item 15, 2026-09-03); ottava sempre auto in `BeatTracker`  
- Standby chiusura Codex PATTERN: sezione **Standby A**; loop WAV: **Standby B** + `HANDOFF_LOOP_DEBUG.md`
