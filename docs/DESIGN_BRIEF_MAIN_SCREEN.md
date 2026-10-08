# Brief di design: schermata principale di Virtual Percussionist

Documento autonomo per Claude Design. Non serve il codice: tutto quello che serve è qui.
Lingua dell'interfaccia: **italiano**. Lingua del brief: italiano.

---

## 1. Cosa è l'app

**Virtual Percussionist** è un percussionista virtuale per iPad (e iPhone). Ascolta una band dal vivo
(microfono/ingresso mixer) oppure un brano caricato, **trova da solo il tempo (BPM), la fase e il battere
(«l'1»)** e suona sopra uno shaker, delle congas, un cembalo e un clap, a tempo e con dinamica che segue la band.

Non è un sequencer e non è una drum machine: **non si programma, si guida**. Il musicista non deve
mai pensare all'app mentre suona; la guarda di striscio e interviene solo con pochi gesti.

## 2. Contesto d'uso (decide tutto il resto)

- iPad su un leggio o su un'asta, **a un braccio di distanza**, spesso **al buio** (palco, sala prove, sera).
- L'utente ha le mani occupate dallo strumento: usa **una mano, un dito, a volte con fretta e senza guardare bene**.
- Si guarda lo schermo per **mezzo secondo**: la domanda è sempre la stessa, *«l'app ha agganciato il tempo?»*.
- Durante il brano si cambiano solo: stile, qualche interruttore di carattere, il volume delle voci,
  l'ottava del tempo (÷2 / ×2), e si dichiara l'1 se l'app lo ha sbagliato.
- Tutto il resto (sorgente audio, clock, buffer, caricamento brano, tema) si imposta **prima** del set,
  in una pagina separata (SETUP) che **non** fa parte di questo lavoro.
- Un tocco sbagliato sul trasporto (START/STOP) a metà brano è il danno peggiore.

**Priorità di design, in ordine:** 1) leggibilità a distanza e al buio, 2) niente tocchi accidentali,
3) pochi controlli, raggiungibili con una mano, 4) bellezza.

## 3. Vincoli di implementazione (importanti)

L'app è **C++ / JUCE**, non web. Quello che disegnate sarà ricostruito a mano con `juce::Graphics`,
un `LookAndFeel` custom e componenti propri. Quindi:

- **Niente effetti web-only** (backdrop blur, CSS filter complessi, ombre multiple sovrapposte, font non
  disponibili). Sono ok: rettangoli arrotondati, pillole, cerchi, tracciati vettoriali, **gradienti lineari/radiali**,
  glow semplice (alone radiale sfumato), trasparenze.
- Font: **Futura Bold** per il numero BPM (display), **font di sistema** per il resto. Indicate solo dimensioni e pesi, non font esotici.
- Icone: solo **SVG semplici monocromatici** (rotella, microfono, FX, freccia).
- Un solo **set di token** (colori, spaziature, raggi, scala tipografica) da cui deriva tutto: consegnatelo come tabella.
- Il layout è **adattivo**: stesso codice per iPad verticale, iPad orizzontale, iPhone, finestra Split View / Stage Manager.
- Bersagli tattili **≥ 44 pt**; ≥ 48–56 pt per START/STOP e ÷2/×2.
- Tema **scuro come principale**, **chiaro** supportato (stesse gerarchie).
- Safe area: su iPhone c'è la Dynamic Island; il contenuto non ci finisce sotto.

## 4. Stato attuale (da cui partiamo)

Palette oggi (scuro / chiaro):

| Token | Scuro | Chiaro |
|---|---|---|
| Sfondo | `#050506` | `#F5F1F6` |
| Pannello | `#0C0C0E` | `#FFFFFF` |
| Superficie controllo (`ink`) | `#121214` | `#E8E3EA` |
| Testo | `#FFFFFF` | `#18141B` |
| Testo secondario (`mute`) | `#A8A8B4` | `#655E6A` |
| Accento | fucsia `#FF2EC8` | idem |
| Meter microfono | fucsia → ambra `#FFA726` → rosso `#FF3B30` | idem |
| Voci | shaker `#AAB0B8`, congas `#D3925C`, cembalo `#E6C43C`, clap `#62B8E4`, triangolo `#2EE8D0` | idem |
| Colpi singoli (6) | `#FF8A1A #FF2A4A #FF2EC8 #9B6BFF #2EE8D0 #FFC857` | idem |

Struttura attuale: **stage** (titolo «VIRTUAL PERCUSSIONIST» con linea fucsia; pillola di stato; corsia di fase;
BPM gigante con ÷2/×2; 4 pallini dei quarti; riga tempo; riga «PARTE …»; TAP), poi due card, **MISURE**
(selettore stile + 6 quadrati: DINAMICA, 1/4, 1/8, 1/16, NATURALE, SWING) e **FEEL** (8 manopole identiche),
in basso una barra con **START** e la manopola **MIC**. In alto a destra la rotella **SETUP**.

**Problemi da risolvere:**

1. Lo stato è sparso su 3 righe di testo piccolo (pillola, riga tempo, riga PARTE).
2. Il titolo occupa una riga che durante il set non serve.
3. ÷2/×2 (26–36 pt) e rotella (~30 pt) sono sotto i 44 pt.
4. Titoli delle card a 10,5 pt; card quasi invisibili sullo sfondo (`#0C0C0E` su `#050506`).
5. MISURE mette sullo stesso piano controlli di natura diversa (stile, suddivisione, interruttori).
6. FEEL mette nella stessa griglia 4 voci con livello continuo e 4 colpi singoli da «sparare».
7. Spaziature incoerenti (4/5/8/10/12/14 pt); il fucsia significa troppe cose (marchio, battere, «fisso», pulsanti attivi).

## 5. Inventario dei controlli (tutti, con etichetta italiana)

**Stato e tempo (stage)**
- Stato del seguimento, una di: `PRONTO`, `ASCOLTANDO`, `CALIBRANDO`, `SEGUENDO E ASCOLTO`, `SEGUENDO`, `ALLINEO TAP`, `TEMPO INCERTO`, `RICALIBRO`, `ATTENDO BATTUTA`, `ATTENDO CHE ATTACCHI`, `IN ASCOLTO - SHAKER OFF`. Alcuni sono «caldi» (agganciato), altri «freddi» (in attesa): servono 3 famiglie visive: **agganciato / in cerca / incerto-perso**.
- **BPM** (es. `128.4`; se non c'è ancora tempo: due barre `– –`). Il numero deve essere l'elemento più grande dello schermo.
- **÷2 / ×2**: cambiano l'ottava del tempo. Mostrano anche se è attiva (a metà / doppio, automatico o manuale).
- **Corsia di fase** («orb»): una barra sottile con un punto che scivola a sinistra se l'app è in ritardo sul clock e a destra se è in anticipo; verde sul tempo.
- **4 quarti**: pallini, il primo (l'1) più grande; quello corrente si accende con alone; sull'1 un anello quando il battere è stato dichiarato a mano.
- **TAP** (tap tempo) e **L'1 È QUI** (dichiara l'1). Oggi il secondo è un'area invisibile su BPM+quarti: vogliamo anche un **pulsante visibile**.
- Note secondarie, solo quando servono: `TEMPO FISSO`, `livello provvisorio`, `a metà (auto)`, `doppio (manuale)`.
- **FISSO / SEGUI** (blocca o segue il tempo) e, se FISSO, `−  [bpm]  +`: oggi in SETUP/compatto; decidete voi se un interruttore SEGUI/FISSO merita un posto sullo stage.

**Come suona**
- **Stile** (menu a tendina custom, prima riga AUTO): MARCHA, ROCK, DANCE, POP, SAMBA, FUNK, REGGAE, BOSSA, DUE-UNO. In AUTO si mostra la confidenza (es. `auto 0.82`).
- **Suddivisione**: `1/4` · `1/8` · `1/16` (esclusivi).
- **Interruttori** (on/off): `DINAMICA`, `NATURALE`, `SWING`.

**Voci** (livello continuo con tap = muta): `SHAKER`, `CONGAS`, `CEMBALO`, `CLAP`. Valore in %.
**Effetti** (colpi singoli, tap = spara/riavvia): `ABSORB`, `HORN`, `UPLIFTER FX`, `RISER 2`. Hanno anche volume.
**EDIT**: modalità che trasforma il tap su una voce/effetto nell'apertura del modale «scegli suono» (le manopole mostrano un anello tratteggiato).

**Ingresso e trasporto**
- **MIC**: gain d'ingresso + meter del livello (fucsia nella banda giusta, ambra poi rosso se troppo caldo, grigio se silenzio).
- **START / STOP**: un solo pulsante grande a due stati.
- **SETUP** (rotella): apre la pagina impostazioni.

## 6. Direzione proposta (punto di partenza, migliorabile)

Principio: **prima il palco, poi la regia**: in alto cosa sta facendo l'app, al centro il tempo, sotto cosa suona, in fondo START.

### iPad verticale

```
┌────────────────────────────────────────────┐
│ ● SEGUENDO        ▁▃▅▆ MIC  [ SPEAKER ]  ⚙ │ ① barra di stato (48 pt)
│                                            │
│           ┌──┐              ┌──┐           │
│           │÷2│   128.4      │×2│           │ ② HERO
│           └──┘     BPM      └──┘           │   BPM enorme, ÷2/×2 da 48 pt
│      ◀━━━━━━━━━━━━ ● ━━━━━━━━━━━━▶         │   corsia di fase
│         ◉      ○      ○      ○             │   4 quarti
│   [  TAP  ]            [ L'1 È QUI ]       │
├────────────────────────────────────────────┤
│ [ Rock ▾  AUTO 0.82 ]  ( 1/4 · 1/8 · 1/16 )│ ③ COME SUONA
│ (Dinamica) (Naturale) (Swing)              │
├────────────────────────────────────────────┤
│  ◯ Shaker  ◯ Congas  ◯ Cembalo  ◯ Clap     │ ④ VOCI
│  [Absorb] [Horn] [Uplifter] [Riser 2]      │    EFFETTI (pad colorati)
├────────────────────────────────────────────┤
│ [          ▶  START          ]  ◉ MIC      │ ⑤ dock fisso
└────────────────────────────────────────────┘
```

### iPad orizzontale: due colonne, START nella colonna destra (dove arriva il pollice)

```
┌──────────────────────────┬─────────────────────┐
│ ● SEGUENDO     ▂▄▆ MIC ⚙ │ [Rock ▾] 1/8        │
│    ÷2   128.4   ×2       │ (Dinamica)(Nat.)(Sw)│
│     ◀━━━ ● ━━━▶          │ VOCI  ◯ ◯ ◯ ◯       │
│    ◉   ○   ○   ○         │ FX   [ ][ ][ ][ ]   │
│  [TAP]     [L'1 È QUI]   │ [      ▶ START     ]│
└──────────────────────────┴─────────────────────┘
```

### iPhone / finestra compatta (< ~560×680)
Dall'alto: stato + meter + rotella · BPM con ÷2/×2 · 4 quarti · stile e suddivisione · 4 mini-manopole voci · START.
Gli **Effetti** stanno dietro un tasto «FX» che apre un foglio dal basso.

### Cosa cambia rispetto ad oggi
- Via il titolo dallo stage live (va nell'intestazione di SETUP).
- Stato unico: una pillola grande (punto colorato + parola) al posto di 3 righe; note secondarie in una riga sotto l'hero solo quando servono; lo stile attivo vive nel selettore.
- Meter del microfono in alto; in basso resta solo il gain.
- MISURE → «Come suona»; FEEL → «Voci» + «Effetti» (pad diversi dalle manopole: sono gesti diversi).
- Il fucsia significa **solo il battere/l'1 e il marchio**; lo stato usa colori semantici (verde agganciato, ambra in cerca, rosso perso).

## 7. Cosa chiediamo a Claude Design

**A. Sistema visivo (tabella di token)**: colori scuro+chiaro (sfondo, 2–3 livelli di superficie, bordo, testo primario/secondario, accento, 3 colori di stato, colori voci/effetti), spaziature (proposta 4/8/12/16/24), raggi (proposta 20 card / 12 controlli / pillola), scala tipografica con dimensioni minime (didascalie ≥ 12 pt, etichette ≥ 14 pt), stati dei controlli (normale, premuto, attivo, disattivato, muto).

**B. Schermate** (almeno, tutte in tema scuro; chiaro per le principali):
1. iPad verticale: stato **agganciato** (SEGUENDO, BPM 128.4, battere 1 acceso).
2. iPad verticale: stato **in cerca** (ASCOLTANDO, nessun BPM → barre `– –`) e stato **incerto** (TEMPO INCERTO).
3. iPad orizzontale: agganciato.
4. iPhone: agganciato + foglio FX aperto.
5. iPad verticale: **tempo FISSO** con nota «a metà (manuale)», mic **troppo caldo** (ambra→rosso).
6. Menu stile aperto (AUTO + 9 stili) e **modale scelta suono** in modalità EDIT.

**C. Per ogni schermata**: indicate misure in pt, gerarchia, cosa è tappabile e la sua area di tocco.

**D. Componenti** isolati con tutti gli stati: pulsante di trasporto START/STOP, pillola di stato (3 famiglie), ÷2/×2, interruttore a pillola, segmented 1/4·1/8·1/16, manopola voce (acceso/muto/EDIT), pad effetto, meter MIC, corsia di fase.

**E. Motion** (solo specifica, ≤ 120 ms per le transizioni): battito dei quarti, scorrimento del punto di fase, pressione dei pad. Niente animazioni decorative.

## 8. Cosa NON toccare

- La pagina **SETUP** (clock, buffer, sorgente, brano, tema, latenza): fuori ambito.
- Il comportamento audio/tempo: la UI mostra e comanda, non cambia come l'app ascolta.
- La gesture «tocco sull'area BPM = dichiara l'1» resta; si **aggiunge** solo il pulsante visibile.
- Il nome dei controlli in italiano e la loro semantica.

## 9. Decisioni aperte (scegliete o proponete alternativa, motivando)

1. Effetti come **pad separati** dalle manopole delle voci: sì o no?
2. **Meter del microfono** in alto accanto allo stato: ok? (il gain resta in basso)
3. «L'1 È QUI» come pulsante visibile **e** area BPM sensibile, o togliere l'area per evitare tocchi accidentali?
4. Titolo/brand sulla schermata live: tolto, o piccolo nella barra di stato?
5. **SEGUI/FISSO** merita un posto sullo stage o resta dietro SETUP?
6. Opzionale: una «modalità palco» che attenua i controlli secondari dopo qualche secondo senza tocchi (se la ritenete utile, specificatela, altrimenti ignoratela).

## 10. Criteri di successo

- Da 1,5 m, in una stanza buia, si legge **BPM e stato** senza esitazione.
- Nessun controllo di uso frequente sotto i 44 pt; START/STOP non sono mai adiacenti a un controllo che si tocca per errore.
- Lo schermo con 8 elementi identici non esiste più: ogni gruppo ha una forma propria.
- Tutto è realizzabile con rettangoli arrotondati, cerchi, gradienti e glow radiale (nessun effetto web-only).
