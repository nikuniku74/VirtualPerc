# Revisione per Claude — item 12, 13, 11

Passa questo file a Claude come brief di review. Lavoro fatto il 2026-09-04
(Grok / Cursor) su `docs/TODO.md` item **12**, poi **13**, poi **11**. Non è
un commit: lo stato è nell'albero di lavoro.

**Non relanciare `VPTests` intera, `VPAlign`, o i probe lunghi senza chiedere.**
I test nuovi stanno in `Tests/TestAiBeat.cpp` (blocco NATURALE dopo il thinning
shaker). Compilati: `VPTests`, `VPRender` e `VirtualPercussionist` host Release.

**Esito test (2026-09-04):** i test di questo lavoro sono tutti verdi:

- `NATURALE is off until the listener asks for it`
- `SPOSTA L'1 nudges and locks; a tap on L'1 e QUI unlocks without nudging`
- `eighths without NATURALE stay an exact eighth grid`
- `NATURALE on eighths adds some sixteenths and does not become a 1/16 part`
- `quarters without NATURALE stay an exact quarter grid`
- `NATURALE on quarters adds some off-eighths, never sixteenths…`
- `NATURALE on sixteenths has nothing to add`
- `cembalo NATURALE lands on the same extra steps as the shaker`
- `NATURALE never puts a conga on the first quarter's down-stroke`
- golden `eighth shaker preserves the pre-feature stroke velocity delay stream bit-for-bit`
- `the default is an eighth-note marcha…` (include `shakerNatural` off)

FAIL visti nella stessa corsa, **non di questo work**: leak (due righe) e i
quattro/cinque RED a 50 BPM dell'item 1 (lasciati rossi apposta). Makeup benches
non portati a termine — non sono di questi item.

Skill da leggere prima: `.claude/skills/percussion-patterns/SKILL.md` (item 11),
`.claude/skills/realtime-tempo/SKILL.md` §3 bar lock (item 13). UI in
`Source/UI/MainComponent.cpp` / `.h`.

---

## Cosa chiedere alla review

1. **Correttezza rispetto al TODO**, non uno stile rewrite. Segnala dove il
   TODO è stato interpretato, non solo dove il codice è brutto.
2. **DSP invariato dove deve esserlo.** Item 12 non deve muovere il clock né
   il commit dello stile al quarto. Item 13 non deve cambiare `barLocked` nel
   tracker, solo il gesto UI. Item 11 non deve toccare le congas né il thinning
   quando NATURALE è off (c'è un golden bit-identical sull'1/8).
3. **Audio thread.** NATURALE è un `atomic<bool>` letto in `processBlock` e
   passato a `GrooveEngine` come gli altri enable. Nessun alloc/lock/I/O.
4. **Cose che non puoi verificare tu:** ascolto (item 11), touch reale su iPad
   (item 12 menu / altezze). Elencale come residue, non come fail.

---

## Item 12 — PARTE dropdown + DINAMICA

**File:** `Source/UI/MainComponent.h` (`StyleSelect`, `StyleMenuOverlay`),
`Source/UI/MainComponent.cpp` (layout PARTE, paint/menu, prefs invariate).

- I dieci `style*` button sono andati. Al loro posto un select custom (non
  `ComboBox`) e **DINAMICA** accanto, stessa altezza.
- Menu: dieci voci AUTO → DUE-UNO, stesso chrome dei `TextButton` (fill ink,
  bordo, underline fuchsia). Tap fuori chiude. Si apre sotto il select, quindi
  non copre START/STOP in modo permanente (PARTE sta sotto TRASPORTO).
- AUTO acceso: il chiuso mostra `AUTO` e accenna lo stile rilevato (il tint
  che facevano i quadrati). DUE-UNO mai scelto dal detector.
- `applyStyle` / `applyStyleAuto` identici: scegliere uno stile spegne AUTO;
  rimettere AUTO riaccende il detector. Commit DSP ancora al prossimo quarto.

**Scelta consapevole, da discutere:** il TODO diceva «Default AUTO selezionato
(`grooveAuto` on, come oggi)». **Come oggi `grooveAuto` è off**
(`EngineSettings`, commento misurato 3/9 in `Types.h`, test
«the default is an eighth-note marcha»). Ho tenuto MARCHA / AUTO off e messo
AUTO prima nel menu. Se si vuole AUTO acceso di default è una riga
(`grooveAuto { true }`) più il test di default — non l'ho fatto perché
contraddice la misura già scritta.

**Da guardare:** `StyleMenuOverlay::resized` (layout e tint), `layoutConsole`
riga PARTE, `refreshStyleButtons`. Overlay a pieno bounds del `MainComponent`,
lista posizionata sull'anchor del select.

---

## Item 13 — «L'1 è QUI» / «SPOSTA L'1»

**Decisione UX: B.** Tap sul tasto acceso = sblocca **senza** nudge. Non più
il 5-click (quattro spostamenti + uno per sbloccare).

| stato | label | tap |
|---|---|---|
| sbloccato | SPOSTA L'1 | ruota un quarto e blocca |
| bloccato | L'1 è QUI (fill fuchsia) | sblocca, conta dov'è |

Un secondo tap (ora sbloccato) sposta di un altro quarto. TAP sul tempo che
dichiara l'1 **continua a bloccare** via `BeatTracker::holdBarDecision` — non
toccato.

**File:** `barControlNudgeOnTap` in `Source/Core/Types.h`; handler
`barButton.onClick` e `refreshBarButton` in `MainComponent.cpp`. Rimosso
`barTapsSinceLock`. Commento di `EngineSettings::barLocked` e
`docs/AUDIO_ENGINE.md` aggiornati (la frase del quinto click era load-bearing).

**Test:** `"SPOSTA L'1 nudges and locks; a tap on L'1 e QUI unlocks without
nudging"` su `barControlNudgeOnTap`. Il banco `bar-lock` (motore, rete che
sposta il downbeat) è invariato: il lucchetto nel tracker non è cambiato.

**Copy:** tenute le due label. Il fill fuchsia + tap-to-unlock dovrebbe far
leggere «stato tenuto», non «tasto rimasto acceso». Se Claude ritiene che
serva una terza riga di hint, dirlo — non l'ho messa perché il tasto è già
stretto.

---

## Item 11 — Shaker NATURALE

**File:** `GrooveEngine::setShakerNatural` / `soundingShaker` in
`Source/Percussion/GrooveEngine.cpp` / `.h`; `EngineSettings::shakerNatural`
(default **false**); tasto **NATURALE** in STRUMENTI (nona cella, dopo
1/16); prefs `shakerNatural`; `VirtualPercussionEngine` inoltra al
`PercussionEngine` come gli altri enable.

**Spec, scritta nei commenti e nella skill:**

- Non è una quinta suddivisione. 1/4 / 1/8 / 1/16 restano griglie esatte.
- Aggiunge eccezioni **dalla tabella già scritta** (`spec.shaker[16]`), non
  inventa un pattern più fitto.
- Su **1/8**: step dispari (e/a), chance `kNaturalEighthChance = 0.22`.
- Su **1/4**: solo gli off-eighth (2/6/10/14), chance `0.30`. Mai i 16th —
  non si salta di due griglie.
- Su **1/16**: nulla da aggiungere.
- Chance scalata `(0.4 + 0.6 * intensity) * dynamics` — gli ornamenti
  spariscono per primi quando la band cala, come i ghost delle congas.
- Una sola rollata per step, condivisa da shaker e cembalo.
- Congas intatte, guard `step != 0` intatto.
- NATURALE off: nessun extra RNG, golden 1/8 bit-identical resta.

**Test** (stesso file, 32 battute dance, intensity 1, humanize 0):

- default off
- 1/8 off = 8 even/bar, 0 odd; on = stessi even, odd > 0 e < metà di un 1/16
- 1/4 off = solo quarti; on = qualche off-eighth, 0 odd, non diventa 1/8
- 1/16 on == 1/16 off
- cembalo NATURALE sugli stessi extra dello shaker
- nessuna conga sullo step 0

**Ascolto.** Render A/B:

```bash
cmake --build build-host --target VPRender
./build-host/VPRender_artefacts/Release/VPRender \
    --style dance --bpm 120 --bars 8 --click --no-congas --sub 8 \
    --out /tmp/shaker-eighth.wav
./build-host/VPRender_artefacts/Release/VPRender \
    --style dance --bpm 120 --bars 8 --click --no-congas --sub 8 --natural \
    --out /tmp/shaker-natural.wav
```

Loop registrati (Standby B): NATURALE vale solo il path sintetico
(`GrooveEngine`). I stem WAV non passano da `eventsAt`.

---

## File toccati (per il diff)

```
Source/Core/Types.h
Source/Percussion/GrooveEngine.h
Source/Percussion/GrooveEngine.cpp
Source/Percussion/PercussionEngine.h
Source/Audio/VirtualPercussionEngine.cpp
Source/UI/MainComponent.h
Source/UI/MainComponent.cpp
Tests/TestAiBeat.cpp
.claude/skills/percussion-patterns/SKILL.md
.claude/skills/realtime-tempo/SKILL.md
.cursor/rules/percussion-patterns.mdc
docs/TODO.md
docs/AUDIO_ENGINE.md
docs/HANDOFF_REVIEW_12_13_11.md   (questo file)
scripts/render_groove.cpp         (--natural, --sub)
```

Cose **non** toccate di proposito: `BeatTracker` (item 13 è solo UI),
`StyleDetector`, clock, ottava, item 5 (cambio stile che perde il tempo).

---

## Checklist review

- [ ] Item 12: il menu non è un ComboBox nativo; DINAMICA è lo stesso toggle
- [ ] Item 12: default AUTO — concordi a lasciare MARCHA, o va acceso?
- [ ] Item 13: tap su lit = unlock senza rotate; TAP tempo ancora locka
- [ ] Item 13: `bar-lock` nel tracker non è stato allentato
- [ ] Item 11: NATURALE off non cambia il golden 1/8
- [ ] Item 11: non diventa un 1/16 / 1/8 pieno; niente conga sul 1
- [ ] Item 11: cembalo segue; congas no
- [ ] Audio thread: solo atomic load
- [ ] Ascolto NATURALE e touch iPad: residue umane
