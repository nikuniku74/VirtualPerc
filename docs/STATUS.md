# Stato dell’app — Virtual Percussionist

Documento operativo. Aggiornarlo ogni volta che cambia cosa si può installare, cosa locka, e cosa resta da provare sul device.

**Data:** 22 agosto 2026  
**Target:** iPadOS 16+, iPad Air M1  
**Versione albero:** 0.1.0

## Pronto per l’iPad

Sì: apri il progetto Xcode, seleziona l’iPad, Run. Nel binario c’è **BeatNet** (pesi GTZAN) + ONNX Runtime iOS (CPU + CoreML).

Dopo questo cambio **riconfigura** iOS (`./scripts/configure-ios.sh`) e reinstalla: il lock SPEAKER/Spotify è cambiato nel C++.

## Ultima verifica automatica (20 agosto 2026)

| Check | Esito |
|---|---|
| Host `VPTests` | **99 passed, 0 failed** — kit/CLICK/quiet SPEAKER 120.0 stabile; TAP e falso aggancio provati in MIXER **e** in IPAD; aggancio di fase a 78/100/138 BPM entro 1 ms; battuta che non riparte; tempo fisso che si ferma; riff lunghi una battuta e frase di quattro; il tap dichiara l'uno |
| ASan + UBSan | Suite intera, **zero segnalazioni** nel nostro codice (restano 64 byte una tantum dentro `libonnxruntime`) |
| ThreadSanitizer | Suite intera, **zero corse** |
| CPU (`VPCpu`) | Callback allo **0,35%** del suo budget; app intera al **2,0% di un core** |
| Tracking, MIXER | errore medio dal tempo vero **2,9 BPM**, ottava giusta **29/30**, attacco sentito +1,4 ms |
| Tracking, IPAD, band dal vivo | errore medio **13,7 BPM** (era 29,7), ottava giusta **25/30** (era 19/30) |
| AI | Snapshot `aiOnnx=1`: motore **ONNX BeatNet**, non lo stub |
| Modello | BeatNet BDA GTZAN, LSTM streaming, `Assets/Models/beatnet.onnx` ~1.6 MB |
| Flags | `VP_USE_ONNX=1`, `VP_ORT_COREML=1`, `VP_HAS_BEAT_MODEL=1` |

## Quanto è stabile una volta agganciato (25 agosto)

Domanda successiva a quella sotto: preso il tempo, si può tenerlo più fermo?
Sì, di un quarto, e non dove pensavo.

### Due ipotesi, misurate e scartate

In IPAD il decoder passa solo il **63%** del tempo a regime dichiarando il tempo
*fisso*, contro l'85% in MIXER — e un tempo «vivo» insegue, quindi oscilla. Le
due condizioni che concedono il regime fisso sembravano le colpevoli:

1. **Il residuo del fit** (`lastFitResidual < 0.05`, un numero senza nome in
   mezzo alla condizione, ed esattamente dove sta il residuo del percorso
   microfono). Rilassato a 0,065 / 0,08 / 0,10: **nessun effetto** — %fisso
   resta 61-63, instabili 20, e il t_2% peggiora. Non era quello.
2. **Il criterio di immobilità** (`kFixedSpread`, 0,9% sulla finestra da 24
   battute). Allargato al 2% funziona sul materiale fermo — %fisso 63 → 75,
   span 4,61 → 3,91 — e **rompe il caso vivo**: su una band con deriva il
   tracker dichiara «fisso» il 68% del tempo invece del 39%. Cioè congela il
   tempo di chi sta accelerando. Scartato.

### Quello che invece funziona: non l'interruttore, la velocità

`kRateLive` è quanto il tempo commesso si muove verso il fit a otto battute a
ogni battuta, ed era **0,35** — un terzo della distanza. Su un microfono in una
stanza quel fit è rumoroso, e inseguirlo così in fretta trasforma il rumore in un
tempo che non sta fermo.

| | prima (0,35) | dopo (0,22) |
|---|---|---|
| IPAD, tempo fisso: span | 4,61 BPM | **3,40** |
| IPAD, tempo fisso: wobble | 0,20 | **0,15** |
| IPAD, band che si muove: span | 5,89 | **5,21** |
| IPAD, band: wobble | 0,26 | **0,22** |
| MIXER, band: span | 12,67 | **10,83** |
| MIXER, band: wobble | 0,33 | **0,27** |
| gradino 120 → 132 | 5,46 s | **5,46 s** |
| accelerando 120 → 140, fase peggiore | 0,178 batt. | **0,181 batt.** |

La riga che decide è la terza: **migliora anche su materiale che si muove
davvero**. Non è un baratto fra stabilità e fedeltà — una velocità che supera il
bersaglio poi risuona, una che non lo supera no.

E non è gratis più in basso: a **0,15** i banchi non migliorano ancora e il
gradino da 120 a 132 passa da 5,46 a 7,28 s. A 0,22 gradino e accelerando sono
identici alla cifra.

Un numero che sembra peggiorare e non peggiora: il t_2% medio in IPAD passa da
16,1 a 19,3 s. I brani «lenti» restano **15 su 30 identici**, e la media sale
perché a 0,22 arriva alla banda del 2% **un brano in più** (24 invece di 23), e
quello che si aggiunge è lento. Media su un denominatore diverso.

Lo span medio in MIXER su materiale fermo passa da 6,48 a 7,44, ed è anche
quello un artefatto: sono le righe a 76 BPM che si scambiano di posto
(`syncopated 76` da 2,69 a 81,3 mentre `pad 76` va da 52,9 a 4,4), cioè la solita
moneta sull'ottava. Le righe vere migliorano: `half-time 128` da 3,88 a 1,48,
`straight 140` da 2,06 a 1,69.

Host `VPTests`: **159 passed, 1 failed** — l'attacco percepito, rosso da prima.

## Perché ci mette a trovare il tempo, e dove sta il resto (25 agosto)

Prima cosa: dividere l'attesa in due, perché finora era un numero solo. `VPProbe`
riporta ora anche **t_val**, l'istante in cui il decoder pubblica un tempo che è
disposto a sostenere. La differenza con t_lock è la macchina a stati del tracker.

| | t_val | t_lock |
|---|---|---|
| MIXER | 2,14 s | 2,32 s |
| IPAD | 2,21 s | 2,38 s |

**Quasi tutta l'attesa è il decoder che decide**; il tracker aggiunge 0,18 s.
Verificato anche al contrario: accorciare il minimo di ASCOLTANDO da 0,70 s a
0,45 e a 0,30 non sposta di un millesimo (1,50 / 2,18 identici). Quel numero non
è il collo di bottiglia e resta com'è.

### Cosa gate il decoder

L'aggancio viene dallo state space, e la soglia è `kAnchorAcquireMargin`.
Spazzolata su trenta brani per modalità:

| margine | MIXER t_lock | MIXER ottave | IPAD t_lock | IPAD ottave (con stanza) |
|---|---|---|---|---|
| 2,0 | 1,49 s | 5 | 2,08 s | 6 |
| 2,5 | **1,50 s** | **4** | 2,17 s | 6 |
| 3,0 | 1,52 s | 4 | 2,25 s | 6 |
| 3,5 | 2,18 s | 4 | 2,32 s | 6 |
| 4,0 | 2,32 s | 5 | 2,38 s | **5** |

Due cose che la tabella dice e che non si vedevano prima:

1. **Sotto 2,5 il guadagno non arriva più a schermo.** Il decoder pubblica a
   0,67 s ma il lock resta a 1,49: il vincolo passa alla macchina a stati.
2. **Appena il margine scende sotto 4, in IPAD compare un'ottava sbagliata in
   più** — e non è l'ambiguità nota dei 76 BPM che sta dappertutto in questo
   file, è `straight 128`, un brano che a 4 legge giusto e a 3,5 legge storto.
   Nessun valore intermedio salva entrambe le cose: a 3,5 la velocità è già
   sparita e l'errore c'è comunque.

### La soglia non è un numero solo

I due percorsi d'ascolto non sono lo stesso segnale — questo documento ha una
sezione intera su quanto sono diversi — e non meritano lo stesso numero. Su una
mandata di linea le attivazioni sono nette e il margine vuol dire quello che
dice; al microfono in una stanza no. Quindi 2,5 sulla linea, 4,0 al microfono:

| | prima | dopo |
|---|---|---|
| MIXER, t_lock | 2,32 s | **1,50 s** |
| MIXER, ottave sbagliate | 5 | **4** |
| IPAD, t_lock | 2,38 s | 2,38 s |
| IPAD, ottave sbagliate | 5 | 5 |

Il MIXER aggancia **il 35% più in fretta** con un'ottava sbagliata in meno, e in
IPAD non cambia niente: le cinque ottave sbagliate restano i cinque brani a
76 BPM di sempre, `straight 128` non compare.

### Cosa resta, e perché non si scende sotto

In MIXER il decoder pubblica a 1,04 s. Il primo secondo di attivazioni è la LSTM
che si scalda — è documentato più sotto, il punto a mezzo battito vale 0,86 nelle
prime tre battute e 0,15 dopo dieci secondi — quindi **1 s è vicino al pavimento
vero**, non una soglia da abbassare. Il resto fino a 1,50 s è la macchina a
stati, e accorciarla non serve (misurato sopra).

In IPAD il decoder pubblica a 2,21 s: lì è il percorso acustico che sfuma gli
attacchi e il margine ci mette di più a formarsi. Quello non è un numero da
cambiare, è il segnale.

Host `VPTests`: **159 passed, 1 failed** — l'attacco percepito, rosso da prima.
`VPDecoderProbe`: 5 fallimenti, gli stessi di prima.

## Il buco senza batteria: cinque tentativi, nessuno spedito (25 agosto)

Seguito della sezione sotto, che aveva lasciato la diagnosi e non la cura. La
cura l'ho provata cinque volte e non la spedisco, e questo è il verbale — serve
a non rifarli.

L'idea era sempre la stessa: durante un passaggio con la batteria fuori il fit
va **largo restando sulla griglia** (residuo 0,033 → 0,064, copertura 0,92–1,00),
mentre un tempo che cambia davvero fa crollare la copertura. Quindi: tieni il
tempo fisso quando il residuo è largo ma la copertura è alta.

| # | variante | buco (peggio) | accelerando (fase) | gradino |
|---|---|---|---|---|
| — | **base** | 63,7 ms | 0,178 di battuta | 5,46 s |
| 1 | soglia assoluta (`kGridHealthyResidual`), blocca tutto | 23,0 ms | — | — |
| 2 | come 1, con tetto di 6 battute | 23,0 ms, poi **184 ms** | — | — |
| 3 | soglia **relativa** al brano, blocca tutto, 6 battute | **25,1 ms** | **0,499** | 6,82 s |
| 4 | come 3 ma lascia vive le vie rapide | **216,9 ms** | 0,179 | 5,46 s |
| 5 | come 3 con tetto di 2 battute a budget | **23,0 ms** | 0,454 | 6,82 s |

Cosa insegna ciascuna:

1. **La soglia assoluta non è una linea.** Su questo percorso il residuo sta a
   0,04–0,06 quasi sempre, quindi «tieni durante un buco» diventa «tieni
   sempre».
2. Con un tetto, la tenuta finisce ma il danno resta: il tempo esce dal buco
   già sbagliato e non c'è più niente che lo corregga — la fase arriva a
   **184 ms** con il regime che si dichiara fisso.
3. La soglia **relativa** («il residuo peggiorato rispetto a quello che *questo*
   brano sta dando») funziona sul materiale — 63,7 → 25,1 ms, coda sana, media
   dentro 20,9 → 14,8 — e **rompe l'accelerando**: la fase peggiore passa da
   0,18 a 0,50 di battuta, cioè il tracker che tiene un tempo che la musica ha
   lasciato.
4. Lasciare vive le vie d'uscita rapide salva l'accelerando e perde il buco:
   **217 ms**. Il buco *raggiunge* quelle soglie, non gli passa sotto.
5. Un tetto corto a budget migliora il materiale (23,0 ms, media dentro 13,2) e
   costa comunque 0,45 di battuta sull'accelerando, perché la tenuta si riarma
   ogni volta che il residuo torna sotto la linea per una battuta.

**La conclusione è il baratto, non la taratura.** Il residuo non separa
«batteria fuori» da «band che accelera» abbastanza presto: nei primi secondi i
due sono lo stesso segnale visto da dentro, ed è esattamente quando la decisione
va presa. Ogni variante che salva il buco costa mezza battuta su un accelerando,
e questa è un'app che deve seguire un batterista dal vivo: il verso giusto del
baratto è quello attuale.

Quello che servirebbe è un discriminante che *non* venga dal fit — qualcosa che
dica «quanto polso c'è nel suono» indipendentemente dalla griglia. `TempoEstimator`
ne ha uno, la salience, e non l'ho provato perché non so che valori prenda su un
accelerando e sarebbe stato il sesto tiro al buio. È il posto da cui ripartire, e
la prima cosa da fare è misurare quei due valori, non scrivere un'altra regola.

## "Quando faccio qualcosa sfasa": misurato (25 agosto)

Segnalazione: con Spotify dalla cassa dell'iPad, una volta preso il tempo, se si
aziona la percussione o si muove un volume sembra che la fase scappi. Ho
costruito il banco che mancava e la risposta non è quella che sembrava.

### Il banco: `VPOps`

Ogni altra sonda qui monta il motore e lo lascia stare. Non è così che si usa.
`VPOps` guida il motore con il brano **più la propria uscita rientrata**, come fa
una stanza, aspetta l'aggancio, e poi esegue un'operazione alla volta misurando
la fase contro la battuta suonata, in millisecondi.

E deve essere una **differenza di differenze**. La prima versione confrontava la
fase prima e dopo l'operazione e leggeva il risultato come colpa
dell'operazione: non lo è. La fase del tracker oscilla da sola di dieci-venti
millisecondi su questo materiale, e l'arrangiamento di default toglie la
batteria per quattro battute su sedici — cosa che nella prima misura ha portato
la fase a **254 ms in una finestra dove non avevo toccato niente**. Quindi il
brano tiene fermo, e la scaletta gira **due volte**: una eseguendo le operazioni
e una senza eseguirne nessuna. Quello che ha fatto l'operazione è ciò che resta
dopo aver sottratto il giro che non ha fatto niente. C'è anche una passata per
ogni singola operazione, così una riga è quell'operazione e non la pila di
tutte quelle prima.

### Le operazioni non sono il problema principale

Con il rientro reale, una passata per operazione:

| | differenza |
|---|---|
| master 0.40 → 1.00 | +6,6 ms |
| congas off | +3,3 ms |
| inseguimento → low | +3,3 ms |
| tutte le altre | fra −14 e +2,9 ms |

Sei millisecondi a 118 BPM sono l'1,3% di una battuta. **Il banco non risolve
sotto i ~10 ms**, perché il giro di controllo oscilla da solo di altrettanto:
le righe piccole vanno lette come rumore, non come effetti.

### Quello che sfasa davvero è il materiale

Stesso brano con il suo arrangiamento, fase per battuta. La batteria esce nelle
battute 8–11 di ogni 16:

| battuta | kit | media | peggio | bpm | regime |
|---|---|---|---|---|---|
| 8 | fuori | 22,7 | 23,0 | 118,03 | FISSO |
| 9 | fuori | 14,9 | 22,2 | 117,71 | FISSO |
| 10 | fuori | 7,3 | 21,8 | 117,12 | **VIVO** |
| 11 | fuori | 44,2 | **63,7** | 116,76 | VIVO |
| 12 | rientra | 53,9 | **66,9** | 117,74 | VIVO |
| … | | | | | |
| 20 | dentro | 20,9 | 23,2 | 118,06 | FISSO |

**Il passaggio senza batteria rilascia il tempo fisso**, e da lì il tracker
insegue: il tempo scivola 118,03 → 116,76 (−1,1%), che è esattamente la fase che
esce dall'altra parte, e ci vogliono otto battute di sovraelongazione per
rientrare. Il controllo sta dentro la stessa misura: nel **secondo** buco dello
stesso brano il regime resta FISSO e lo stesso passaggio costa meno di 15 ms.

Un brano vero — Spotify — ne è pieno: stacchi, intro senza kit, strofe piano. È
lì che si sente sfasare, e capita di stare toccando qualcosa nello stesso
momento.

### Il tentativo di correzione, e perché non l'ho spedito

Dai due numeri che il decoder già calcola, il passaggio si riconosce: il residuo
del fit sale (0,033 → 0,041 → 0,064) mentre la copertura resta alta (0,92–1,00).
Le battute rilevate stanno sulla griglia e sono solo mal piazzate — sono pad e
basso, non un kit. Un tempo che cambia davvero fa l'opposto, e il file lo dice
già dove spiega la via d'uscita rapida: appena la griglia è sbagliata il gate
on-grid smette di ammettere battute, quindi è la **copertura** a crollare.

Tenere il tempo fisso quando il residuo è largo ma la copertura è alta **risolve
il buco**: peggio 63,7 → 22,99 ms, regime FISSO per tutto il passaggio. E
introduce un guasto peggiore venti battute dopo: la fase se ne va a **184 ms**
con il tempo bloccato a 116,4 e niente che possa più correggerlo. Il motivo è
misurato: su questo percorso il residuo sta a 0,04–0,06 quasi sempre, quindi
`kGridHealthyResidual` (0,035) non è una linea che separa "prova debole" da
"prova buona" — con quella soglia "tieni durante un buco" diventa "tieni
sempre". Un tetto di sei battute non basta a salvarla.

Non spedito. La forma della correzione giusta si vede: la soglia dev'essere
**relativa** — il residuo peggiorato rispetto a quello che *questo brano* stava
dando — non un numero assoluto. È il prossimo passo, ed è corto.

### Cosa è cambiato invece

Un difetto vero, trovato leggendo e non misurando: `pushOutputToRing` metteva
nel riferimento del cancellatore del rientro il mix **prima** del master fader,
mentre dalle casse esce quello dopo. Il riferimento era un segnale che nella
stanza non c'è mai. Un fader fermo viene assorbito dal guadagno che il
cancellatore stima — ecco perché è sopravvissuto fin qui — ma un fader che si
**muove** no, e ogni tocco del volume lasciava la stima sbagliata finché non
riconvergeva, con la nostra parte nell'analisi nel frattempo. Sul banco le due
righe del master passano da +6,6 ms a −1,7 e −7,2.

Il click del CLICK TEST resta deliberatamente **fuori** dal riferimento: viene
aggiunto all'analisi apposta, ed è tutta la funzione.

E due diagnostiche che non c'erano e senza le quali questa indagine non si
poteva fare: `fitResidual` e `fitCoverage` arrivano ora fino allo snapshot.

Host `VPTests`: **159 passed, 1 failed** — l'attacco percepito, rosso da prima.

## iPhone e Mac fra le destinazioni, Vision no (25 agosto)

Una sola build iOS, e le quattro destinazioni di Xcode decise invece che lasciate
ai default - che dicono di sì a tutte:

| destinazione | | come |
|---|---|---|
| iPhone | **sì** | `TARGETED_DEVICE_FAMILY` 1 |
| iPad | **sì** | `TARGETED_DEVICE_FAMILY` 2 |
| Mac (Designed for iPad) | **sì** | `SUPPORTS_MAC_DESIGNED_FOR_IPHONE_IPAD` |
| Apple Vision | **no** | `SUPPORTS_XR_COMPATIBLE=NO` |

Vision è fuori di proposito: tutta l'analisi è tarata su due percorsi di
microfono - una ripresa ravvicinata del kit e la cassa dell'iPad nella sua
stanza - e l'array di un visore non è né l'uno né l'altro. Installabile non è la
stessa cosa che funzionante, e chi compra non vede la differenza.

**Correzione (25 agosto).** La prima versione di questa tabella diceva che Mac e
Vision si decidevano con due build setting, `SUPPORTS_MAC_DESIGNED_FOR_IPHONE_IPAD`
e `SUPPORTS_XR_COMPATIBLE`. **Non è vero**, e le pagine di aiuto di Apple sono
esplicite: un'app iPhone/iPad è offerta su Apple Vision Pro e sui Mac Apple
silicon **per default**, e la si toglie in **App Store Connect**, sotto *Pricing
and Availability*. Non esiste un build setting che spenga Vision; uno che sembra
farlo viene semplicemente ignorato. Quelle due righe erano una supposizione
scritta come se fosse un fatto, e sono state tolte.

Quindi la lista vera è due voci in due posti:

- **Xcode / CMake**: `TARGETED_DEVICE_FAMILY "1,2"`. È questa che fa comparire
  iPhone, e finisce nell'Info.plist come `UIDeviceFamily`.
- **App Store Connect** → Pricing and Availability: *iPhone and iPad Apps on
  Apple Silicon Mac* selezionato, *iPhone and iPad Apps on Apple Vision Pro*
  deselezionato.

`SUPPORTED_PLATFORMS` è impostato a `iphoneos iphonesimulator`, ma solo per
tenere in ordine l'elenco delle destinazioni **dentro Xcode**: con l'SDK
visionOS installato, Xcode aggiunge da solo "Apple Vision (Designed for iPad)" a
qualunque target compilato contro l'SDK iOS. È quello che offre l'IDE, non quello
che può installare chi compra.

E la ragione per cui non si vedeva niente cambiare: **il progetto Xcode è
generato**. Le impostazioni stanno in `build-ios/VirtualPercussionist.xcodeproj`,
quindi una finestra aperta prima della modifica mostra ancora le destinazioni
vecchie. Serve `rm -rf build-ios && ./scripts/configure-ios.sh`.

### Il layout su uno schermo da telefono

Due cose lo reggevano solo per caso su un iPad e non l'avrebbero retto su un
telefono:

1. **Il margine era un numero, non il margine vero.** Ogni pagina a schermo
   intero partiva da `reduced(24, 20).withTrimmedTop(16)`, che su un iPad libera
   la status bar e su un telefono lascia il tempo sotto la Dynamic Island in
   ritratto e il trasporto sotto un angolo arrotondato in orizzontale. Ora
   `MainComponent::safePadded` prende, lato per lato, **il più grande fra quel
   margine e l'inset che il sistema dichiara**. Prendere il massimo invece di
   sommarli è ciò che lascia l'iPad identico a prima.
2. **Le righe del palco non si restringevano.** Fra minimi e spazi fissi il
   blocco vuole circa 340 punti; un iPad ce li ha sempre, un telefono in
   orizzontale ne ha circa 290, e il metro e la riga del microfono cadevano
   semplicemente fuori dal fondo. Ora scalano insieme.

Misurato sulle geometrie vere (colonna → palco → fattore di scala):

| schermo | palco | scala |
|---|---|---|
| iPad Air M1 ritratto | 772×522 | **1,00** |
| iPad Air M1 orizzontale | 498×722 | **1,00** |
| iPhone 15 Pro ritratto | 345×330 | 0,94 |
| iPhone 15 Pro orizzontale | 323×294 | 0,87 |
| iPhone SE ritratto | 327×240 | 0,71 |
| iPhone SE orizzontale | 272×277 | 0,82 |

### Cosa non è stato provato

**Niente di tutto questo è stato visto su un telefono vero**, e due cose vanno
guardate lì e non qui: come sta il layout a 0,71 su un SE (ci sta, ma "ci sta"
non vuol dire leggibile), e la modalità SPEAKER, che su un iPhone è una cassa più
piccola e un microfono molto più vicino - un percorso acustico diverso da quello
su cui il cancellatore del rientro e il makeup sono stati misurati. La build iOS
non è compilabile su questo host: la modifica al CMake e il ramo `JUCE_IOS` del
layout sono verificati per tipo contro gli header di JUCE, non eseguiti.

## Quanto ci mette a trovare il BPM (24 agosto)

Due cose, e la prima e' un difetto vero che si vede solo se si prova l'app come
la si usa davvero.

### La stanza vuota contava come prova

Tutte le sonde partivano con la musica al campione zero. Sul dispositivo non
succede mai: l'app ascolta da quando la si apre, quindi quando la band attacca
l'analisi gira da minuti su una stanza vuota — e ogni guardia dentro
`TempoEstimator` contava **frame**, non musica. Con quaranta secondi di rumore
di stanza davanti (misurato sulla rete vera) l'estimatore nomina un tempo con
salience 0.29 e dichiara il livello *deciso* **un decimo di secondo** dopo la
prima battuta, senza averne esaminata nemmeno una. Anche lo state space e'
avvelenato: all'attacco sta gia' a 107 o 136 BPM con margine 2.7-8.0, e avendo
una penalita' di cambio poi lo difende.

Il conto: `VPProbe --pre 20` su un brano a 128 in MIXER agganciava a **-14,6 s**
(cioe' cinque secondi dentro la stanza vuota), a 150 BPM, in regime *fisso*, e
ci metteva **27 s** a trovare i 128 veri — contro 5,7 s partendo da freddo.

Ora il livello d'ingresso *prima* del makeup viene guardato: quando sale di
diciotto decibel uscendo da uno stato davvero silenzioso (ventiquattro sotto il
piu' forte che quell'ingresso raggiunge), l'analisi ricomincia a contare. Non
sulla discesa — una strofa piano non e' una canzone nuova — e non se a farlo
salire siamo stati noi, perche' quello che suoniamo rientra dal microfono e il
cancellatore non sempre lo trova. Nello stesso momento il makeup viene
ri-agganciato al livello nuovo invece di scivolarci in otto decimi di secondo, e
lo stato ricorrente della rete viene azzerato: una partenza a freddo comincia
con quello a zero, ed e' la condizione in cui e' stato misurato tutto.

Il contratto sta in due test: `ripartenze` deve essere **0** dentro un brano
(anche con dodici decibel di buco per otto secondi) e **1** per brano quando la
band attacca.

### Il livello lo sapeva gia' lo state space

`TempoEstimator` non riporta niente finche' il buffer non contiene cinque
periodi dell'ottava *sotto* il suo vincitore: dieci battute, 4,3 s a 140 BPM e
7,9 s a 76. Misurato, il tempo fino a FOLLOWING era **esattamente** quello:

| BPM | 10 battute | t_lock misurato |
|---|---|---|
| 76 | 7,89 | 8,3 |
| 92 | 6,52 | 6,8 |
| 104 | 5,77 | 6,2 |
| 118 | 5,08 | 5,5 |
| 128 | 4,69 | 5,1 |
| 140 | 4,29 | 4,6 |

`BeatHmm` intanto accumula dal primo frame, e sulle attivazioni vere nomina il
livello giusto con margine a **1,2-1,7 s** (10 dump su 12; i 2 sbagliati sono i
soliti 76 letti 152, che sbaglia anche il fold). Serviva solo al decoder per
ripiegare la risposta del comb — cioe' arrivava *dopo* la cosa che si stava
aspettando. Ora, se il comb non puo' ancora parlare e lo state space e' chiaro,
il decoder aggancia da li'. Del suo prende **il livello e una griglia, non il
numero**: i suoi periodi sono a frame interi, quindi legge circa il 2% alto
(120,0 per 118, 142,9 per 140), e il tempo passa ai minimi quadrati dalla quarta
battuta. La soglia per agganciare e' piu' alta di quella per ripiegare
(`kAnchorAcquireMargin` 4 contro 2): ripiegare si disfa al refresh dopo,
agganciare no.

Sul decoder da solo, a 140 BPM: primo tempo valido **0,88 s** con lo stato
contro **4,30 s** col solo fold.

### Le misure

`VPProbe` ha ora `--pre <sec>` (stanza vuota davanti, t=0 e' la prima battuta) e
`--sync`, che aspetta che l'analisi finisca il blocco prima di darle il
successivo. Serviva: senza, lo stesso identico binario dava span medio 8,7 / 9,2
/ 12,1 BPM su tre giri, un rumore di fondo piu' largo di quasi tutte le
differenze che si vogliono misurare. Con `--sync` il giro e' **ripetibile** e
gira quaranta volte piu' in fretta.

Trenta brani, tempo fisso, prima e dopo:

| MIXER | da freddo | | con 20 s di stanza | |
|---|---|---|---|---|
| | prima | dopo | prima | dopo |
| t_lock medio | 5,57 s | **2,64 s** | | |
| ottava sbagliata | 5 | **4** | 4 | 4 |
| instabili (>1,5) | 13 | **5** | 12 | **9** |
| lenti (>12 s) | 9 | **8** | 14 | **7** |
| span medio | 15,67 | **6,36** | 12,86 | **8,69** |

| IPAD | da freddo | | con 20 s di stanza | |
|---|---|---|---|---|
| | prima | dopo | prima | dopo |
| t_lock medio | 4,13 s | **3,07 s** | | |
| ottava sbagliata | 5 | 5 | 5 | 5 |
| instabili (>1,5) | 22 | **18** | 22 | **19** |
| lenti (>12 s) | 17 | **15** | 19 | **17** |
| span medio | 5,89 | **3,83** | 5,34 | **2,91** |

L'unico numero che peggiora e' l'errore medio nei due casi col pre-roll (3,52 →
8,29 in MIXER, 11,66 → 13,22 in IPAD): a parita' di ottave sbagliate sono
*brani diversi* a sbagliarle, e quella metrica e' dominata da quali. Tutto il
resto e' uguale o meglio.

Host `VPTests`: **156 passed, 1 failed** — quello che resta rosso e' l'attacco
percepito, ed era rosso anche prima. L'allineamento di fase, che prima falliva,
ora passa; il test del TAP e' stato riscritto perche' la sua *premessa* non e'
piu' vera (la battuta automatica su quel brano era giusta lo 0% del tempo prima
e il 100% dopo, quindi non puo' piu' servire da controprova), non la sua tesi.

### La stanza vuota: agganciata sì, suonata no (24 agosto, seguito)

Il difetto sotto e' rimasto - l'app **aggancia** la stanza vuota e non c'e' modo
di impedirglielo - ma il danno vero era un altro: se in quel momento premi START
lo shaker parte davvero, al tempo della stanza, su un palco vuoto.

Quello si chiude senza dover distinguere una stanza da una band, perche' non
serve la domanda difficile ("cos'e' questo?") ma quella facile: **questo ingresso
e' cambiato da quando ho aperto l'app?** Una stanza da sola non cambia mai; una
band che attacca sì, ed e' esattamente l'evento che il guardiano di livello gia'
riconosce. Quindi START arma e tiene la percussione muta finche' l'analisi non ha
visto l'ingresso *partire*, e sullo schermo c'e' scritto **ATTENDO CHE ATTACCHI**.

Misurato: quindici secondi di stanza con lo shaker armato dal primo campione →
**zero colpi**, stato `ATTENDO CHE ATTACCHI`, BPM 93,4 visibile ma non suonato.
Poi attacca la band → 90 colpi, 120,00 BPM.

**Il punto cieco e' voluto e va detto:** un brano gia' in corso quando l'app viene
aperta non "parte" mai, quindi aspetta. Lo libera un **TAP**, o il tempo messo a
mano con FISSO. Niente timeout, di proposito: uno abbastanza lungo da fare da
guardia e' abbastanza lungo da dare fastidio, e uno abbastanza corto da tollerare
riporta lo shaker che suona a sala vuota.

E una conseguenza sui banchi che vale la pena scrivere: **ogni sonda e ogni test
partiva con la musica al campione zero**, cioe' proprio nel punto cieco. Con la
percussione tenuta fuori quei banchi non suonavano piu' niente - e uno dei test
sull'attacco percepito passava perche' non c'era piu' niente da misurare, che e'
il modo peggiore di passare. Adesso i due renderer dei test, `VPTiming` e
`VPProbe` mettono un secondo di stanza davanti: `--pre` era gia' li' per questo,
e il default e' un secondo invece di zero. Il grano dei numeri si sposta di poco
(t_lock medio da freddo 2,32 s in MIXER e 2,38 s in IPAD), ma adesso i banchi
misurano quello che fa un dispositivo.

Host `VPTests`: **159 passed, 1 failed** - l'attacco percepito, rosso da prima.

### La stanza vuota la aggancia ancora, e non l'ho risolto

L'app **aggancia la stanza vuota**: 30 brani su 30 in MIXER, prima che qualcuno
suoni, e in una misura diretta arriva a FOLLOWING a **99 BPM con confidenza
0.91** davanti a un microfono che non sente nessuno. La ripartenza lo butta via
entro un terzo di secondo dall'attacco vero, quindi il numero sbagliato dura
solo finche' nessuno suona — ma se in quel momento premi START lo shaker parte
davvero, al tempo della stanza.

Ho provato a chiuderlo e mi sono fermato. Il vincolo che lo rende difficile e'
misurato: **la band piu' piano che i test pretendono agganci e' piu' piano della
stanza che inganna il tracker** (picco 0,0023 contro 0,0060). Quindi nessuna
soglia sul livello puo' funzionare, e la regola deve riguardare la *forma* di
quello che dice la rete. `VPRoom` misura i quattro candidati sulle due strade
d'ascolto:

| | stanza | band |
|---|---|---|
| p50 (pavimento dell'attivazione), MIXER | 0,051–0,079 | 0,001–0,033 |
| p50, IPAD | 0,055–0,106 | 0,019–**0,048** |
| p50/p95 | 0,21–0,80 | 0,00–**0,18** |
| frame sopra il gate 0,40 della rete | 0,0–**0,9%** | **1,5%**–9,1% |

Il pavimento separa benissimo in MIXER e **fallisce in IPAD**, dove il percorso
acustico chiude i buchi fra una battuta e l'altra. Il gate a 0,40 separa in tutte
e due ma 0,9% contro 1,5%, e la quota della stanza arriva tutta in una raffica
nei primi secondi — cioe' esattamente quando la decisione viene presa: a 3,2 s la
stanza aveva gia' fatto registrare **nove battute**, e il comb nominava 196 BPM,
che a quel ritmo ci sta. Ho provato anche a chiedere che le battute rilevate
stessero su una griglia: non funziona *prima* dell'aggancio, perche' il gate
on-grid non e' ancora attivo e sulla musica gli ottavi fanno sembrare gli
intervalli peggiori di quelli della stanza (rms 0,26–0,46 contro 0,15).

Nessuno dei quattro ha un margine che valga la pena spedire, e l'errore nell'altra
direzione — un'app che sullo stresso palco si rifiuta di seguire una band che
suona piano — costa molto piu' di un numero sbagliato sullo schermo prima del
concerto. Quindi resta aperto, con la misura in `scripts/probe_room.cpp` per chi
ci riprova. Le due strade che non ho battuto: una regola *di prodotto* invece che
di rilevamento (START che non arma finche' l'ingresso non e' cambiato da quando
l'app e' stata aperta — ma se l'app viene aperta a musica gia' in corso non
cambia mai), oppure un modello di rumore vero al posto di questo, che e' rumore
filtrato a un polo e cioe' il caso ostile.

## Il core del tempo, rivisto (24 agosto)

Audit completo su fase e aggancio, con le misure e il resto in
[docs/CORE_TIMING_AUDIT.md](CORE_TIMING_AUDIT.md). Sei cose trovate, sei
corrette. Da riprovare sul device: aggancio su traccia gia' in corso, cambio
traccia su Spotify, e START dopo uno STOP lungo.

| | prima | dopo |
|---|---|---|
| Rumore di fase dal decoder (22 ms di jitter in ingresso) | 22.2 ms rms | **8.7 ms rms** |
| Scatti di fase > 0.05 di battito, in 35 s | 11-49 | **0** |
| 168 BPM con ottavi a 0.45 | 168.00 BPM, **mezzo battito fuori per sempre** | in fase |
| Mezzo battito di scarto da chiudere (HIGH / LOW) | 4.85 s / 13.34 s | **2.81 s / 5.92 s** |
| Lo stesso, a shaker fermo | uguale | **immediato** |
| Trim su una canzone 1 BPM lontana | si ferma a 0.500 | **1.000** |

In breve: la fase esce dal fit come il tempo e non piu' dall'ultimo picco; il
ripiegamento dell'attivazione trova una griglia ancorata sul controtempo, che
prima si difendeva da sola; il tetto di sterzata si apre quando l'errore e'
grande e la griglia si sposta e basta quando non suona niente; il trim chiude
tutto l'errore invece di meta'; e il rilevatore di "canzone nuova", che non
poteva scattare, e' stato sostituito dal decoder che dice quando butta via la
griglia.

Host `VPTests`: **128 passed, 0 failed**. Sonda: `VPAlign`.

## Il link USB iPad -> X-Air non regge, e non e' l'app (22 agosto)

Chiuso il capitolo sopra: la causa **non e' nel nostro codice**. Isolata cosi',
e il risultato e' netto:

| prova | esito |
|---|---|
| App **chiusa** dal selettore, solo Spotify, iPad -> X-Air via USB | cade dopo ~1 s |
| Senza cavo USB, anche con l'app aperta | regge |
| Musica in un ingresso normale del mixer con un cavo audio | regge |
| USB usata come **uscita** | cade dopo ~1 s |

Cioe': muore il collegamento audio USB fra iPad e X-Air, qualunque cosa lo stia
usando. L'app ci finiva dentro solo perche' era una delle cose che lo usavano.

Non c'e' un "driver dell'iPad" da aggiornare: iPadOS non ha driver per singola
periferica audio, usa la classe generica **USB Audio Class 2.0**. La leva sta
nell'alimentazione della porta, nella banda che il flusso chiede e nel firmware
del mixer, non nel software.

Il mixer e' un **XR18**: 18 in / 18 out su USB. Provato senza esito, e in
quest'ordine sono cadute le ipotesi:

| provato | esito |
|---|---|
| 44.1 e 48 kHz, e i due clock allineati | cade lo stesso |
| iPad alla corrente | cade lo stesso |
| hub, con l'iPad alimentato **mentre** e' collegato | cade lo stesso |

Con l'alimentazione esclusa da quest'ultima prova, quello che resta e' il flusso:
18 x 18 canali sono un carico isocrono pesante, e iPadOS ha limiti pratici su
quanti canali regge. Non e' una cosa su cui il software dell'app abbia una leva -
iOS non espone un modo per chiedere a una periferica solo due dei suoi diciotto
canali.

**Conclusione operativa: la porta USB dell'XR18 non e' una strada per l'iPad.**
Il rig e' una interfaccia **2x2 class-compliant** fra iPad e mixer: uscita su un
canale di linea, una mandata del banco sul suo ingresso. Copre ingresso e uscita
insieme, che e' il requisito, e l'app funziona cosi' com'e' - CLOCK su AUTO segue
l'interfaccia, sorgente MIXER. Che l'ingresso analogico del banco funzioni e'
gia' stato verificato.

Resta una prova che vale la pena fare comunque, perche' cambia la natura del
problema: **l'USB dell'XR18 su un computer.** Se cade anche li', non e' un limite
di iPadOS ma una porta o un cavo guasti, ed e' una riparazione invece che un
vicolo cieco.

Quello che l'app ha guadagnato lungo questa indagine resta valido e utile a
prescindere - si rialza da sola quando iOS le porta via il device, invece di
restare muta finche' qualcuno non cambia una impostazione - ma **non impedisce
il crollo**, perche' il crollo non e' suo.

## L'audio si ferma dopo qualche secondo con l'X-Air attaccato (22 agosto)

Sintomo: iPad collegato via USB al mixer X-Air, **entrambi i clock sulla stessa
frequenza**. Si sente per un attimo, poi si blocca. Per farlo tornare bisogna
cambiare frequenza sulla pagina impostazioni — che riapre il device — e dopo
qualche secondo si blocca di nuovo. Attaccare l'iPad alla corrente non cambia
niente.

**Che a farlo tornare sia la riapertura del device e' tutta la diagnosi.** Quando
iOS riavvia il suo media server, tutto quello che il processo possiede di audio
diventa invalido: sessione, audio unit, tutto. Va ricostruito. JUCE la notifica
la riceve, ma le risponde cosi':

    isRunning = enabled;
    setAudioSessionActive (enabled);
    AudioOutputUnitStart (audioUnit);   // l'unit di prima del reset

cioe' fa partire un handle a una cosa che non esiste piu'. L'app resta muta
finche' qualcosa non ne costruisce una nuova — e cambiare il clock era l'unica
cosa nell'app che lo facesse. Un'interfaccia USB class-compliant e' uno dei modi
piu' affidabili di provocare il reset, ed e' per questo che succede col cavo
attaccato e non senza.

Due risposte, perche' la prima puo' sfuggire:

1. `vp::setMediaServicesResetHandler` osserva direttamente
   `AVAudioSessionMediaServicesWereResetNotification` e ricostruisce: prima la
   sessione (dopo un reset non ha piu' niente di quello che le era stato
   impostato), poi `closeAudioDevice()` + `restartLastAudioDevice()` — chiudere,
   non riaprire, perche' `setAudioDeviceSetup` si tiene l'oggetto device e la
   sua unit morta.
2. Un **watchdog** sul thread messaggi conta le callback audio. Un device che
   dovrebbe girare e non chiama da un secondo viene ricostruito lo stesso,
   qualunque cosa lo abbia fermato. Minimo due secondi fra una ricostruzione e
   l'altra.

Il conteggio e' sulla pagina impostazioni, **STATO / riavvii**. Deve stare a 0:
se sale da solo, il rig sta perdendo il device audio di continuo e la
ricostruzione lo sta tamponando, non risolvendo.

### E una cosa trovata cercando questa

Il guadagno di make-up dell'analisi era pilotato dal livello **prima** della
sottrazione del rientro. Da quando la sottrazione funziona davvero, questo
teneva l'analisi sotto il livello su cui la rete e' stata validata, esattamente
di quanto era stato tolto: misurato su un feed con rientro e parte al massimo,
**0,045 contro 0,076** con la parte in silenzio — stessa band, piu' piano, per
un motivo che la rete non puo' conoscere. Ora il make-up legge il livello del
segnale a cui viene applicato: **0,0775**, alla pari.

Tenuto da `analysis-level` nella suite, che fallisce con l'ordine vecchio.

### Cosa non e' stato verificato sul device

Non ho un iPad ne' un X-Air: la diagnosi e' sul meccanismo (il codice JUCE che
gestisce il reset e' li' da leggere), non su una riproduzione. Se dopo questo
cambio il problema resta, **guarda `riavvii` sulla pagina impostazioni**: se sale
la causa e' quella e il tamponamento non basta; se resta a 0 il device non si sta
fermando e la causa e' altrove.

## Il tracker seguiva sé stesso (22 agosto)

Tre sintomi, una causa sola:

- alzando il volume di shaker e percussioni **il tempo rallenta e si perde**; abbassandolo resta fedele;
- quando finisce un brano e ne comincia un altro, **si riaggancia solo se prima premi STOP**;
- mentre tiene il tempo e cerca di allinearsi, **accelera o decelera troppo in fretta**.

I primi due sono la stessa cosa: quello che l'app suona rientra in quello che
l'app ascolta. Un pezzo di percussione che rientra e' un impulso perfettamente
d'accordo con il clock, quindi il tracker si sente dire che ha ragione ovunque
si trovi — e piu' forte suona la parte, piu' l'analisi *e'* la parte. STOP
zittisce la parte, ed e' zittendola che torna a sentire il brano.

Misura: `leak-residual` nella suite dei test da in pasto al motore **solo la sua
uscita ritardata** e riporta quanto ne sopravvive nel segnale di analisi. 1.000
significa un tracker che segue soltanto il proprio shaker.

| | prima | dopo |
|---|---|---|
| IPAD, blocco 1024 | 0.62 | **0.063** |
| IPAD, blocco 4096 | 0.57 | **0.021** |
| **MIXER, blocco 1024** | **1.000** | **0.063** |

Tre difetti:

1. **La sottrazione girava solo in SPEAKER.** Il rientro era modellato come la
   cassa dell'iPad nel suo microfono. Ma un mixer restituisce l'uscita
   direttamente sul return, ed e' il rig con l'X-Air: **non veniva sottratto
   niente**.
2. **Cancellava solo la parte alta.** Il riferimento era passa-alto a 1.5 kHz —
   giusto per una cassa di tablet, che il basso non ce l'ha — quindi su un mixer
   le congas passavano intatte. Ora sono due bande, risolte insieme.
3. **Poteva inventarsi un rientro inesistente.** Il guadagno stimato veniva
   clampato a zero *prima* di essere smorzato: su un blocco il fit fra due
   segnali scorrelati non e' zero, e' zero piu' qualche per cento di rumore, e
   tenerne solo la meta' positiva lo media a un guadagno positivo stabile. Ora
   il fit con segno viene smorzato prima e clampato solo dove si applica, con un
   gate sull'energia spiegata.

### Il terzo sintomo: l'anello di fase

Il clock corregge la fase per *velocita'*, non spostando la griglia. Ogni misura
esistente pero' leggeva `tick.tempoBpm`, cioe' il tempo **prima** della
correzione: gli impulsi escono a `tempo * (1 - steer)`, quindi tutta l'escursione
era invisibile ai test.

Con un decoder la cui fase porta tre centesimi di battuta di errore proprio —
quello vero li porta, e si aggiorna sei volte al secondo — l'anello stava **al
limite in entrambe le direzioni, in permanenza**: ±6 BPM a 120, 3,7 BPM rms, su
una band che non si muoveva. Due cause: il guadagno arrivava al limite con un
errore di 0,022 di battuta (piu' piccolo dell'incertezza di cio' che misurava), e
lo smoothing era **per blocco**, quindi la costante di tempo dell'anello era la
dimensione del buffer — 4,2 BPM rms su 64 campioni contro 2,2 su 1024, stessa
musica. Con il buffer ora scegliibile dalla pagina impostazioni, era un settaggio
che ritarava il tracker di nascosto.

| | wobble rms a 120 | peggiore | 64 vs 1024 |
|---|---|---|---|
| prima | 3,7 BPM | ±6,0 | 4,2 / 2,2 |
| dopo | **0,15 BPM** | 1,2 | **0,14 / 0,19** |

Tenuti da `phase-steer` nella suite.

### Brano nuovo: adesso riallinea anche la fase

Su un cambio di brano veniva ritarato il **tempo** e basta; la fase restava
affidata all'anello di steering, che e' fatto apposta per chiudere centesimi di
battuta — e mezza battuta non e' centesimi. Ora, quando la ritaratura si chiude e
la fase e' fuori di piu' di 0,15 di battuta, viene agganciata come al primo lock
e i voti sulla battuta vengono azzerati (sono prove su un brano finito). Questo
e' ragionato, non misurato: il rientro era la causa dominante del sintomo.

### x2 e :2 in automatico

L'ottava metrica e' l'unica cosa del tempo che il segnale non risolve (le misure
stanno su `BeatDecoder::userOctave`). Ora la decide una regola, con i due tasti
come override: **tieni la pulsazione su cui si suona la parte fra 76 e 168 BPM**.
La banda e' poco piu' larga di un'ottava e quel margine e' l'isteresi (un tempo
appena dimezzato da 168 cade su 84, non su 76). Un cambio di livello viene tenuto
2,5 s prima di essere preso.

**Non e' una soluzione al problema dell'ottava.** Dove mettere la banda e' un
giudizio su come deve stare una parte, non una misura, e non e' stato validato
su ascoltatori. E' limitato, reversibile con un tocco, e la riga del tempo dice
quando la scelta e' dell'app: `a meta' (auto)`.

Da provare sul device: con l'X-Air, alza lo shaker al massimo su un brano fermo e
guarda se il BPM sta fermo; poi cambia brano senza premere STOP.

## Crack all'avvio e Spotify che si ferma (22 agosto)

Sintomo: all'apertura dell'app si sente un **crack**, e se stava suonando qualcosa (Spotify, Apple Music) **la riproduzione si ferma**. Rig: iPad + Behringer X-Air via USB, ingresso e uscita insieme, mixer a 48 kHz.

Non era la sessione audio non condivisa: la categoria era gia' `PlayAndRecord` con `MixWithOthers`. Erano tre cose che riconfigurano l'hardware all'avvio, e su un'interfaccia esterna ogni riconfigurazione e' un clic:

1. **JUCE che cerca i sample rate provandoli.** Senza `JUCE_IOS_AUDIO_EXPLICIT_SAMPLERATES`, `juce_Audio_ios.cpp` scopre cosa supporta la route **impostandola**: `setPreferredSampleRate` a 4 kHz, poi 192 kHz, poi ogni kilohertz in mezzo — circa **190 cambi di clock di fila**, a ogni apertura del device. Questo e' il crack, ed e' anche quello che strappa il sample rate all'app che stava suonando sulla stessa route. Ora i rate sono **dichiarati** (44.1 / 48 / 88.2 / 96) e lo sweep non parte mai.
2. **Sessione dopo il device.** `configurePlaybackSession()` veniva chiamata *dopo* che JUCE aveva aperto il device, e chiedeva 48 kHz fissi e un buffer di 256 comunque: con il mixer a 44.1 lo faceva ripartire, con il mixer a 48 riscriveva valori gia' giusti. Ora `vp::prepareAudioSession` gira **prima**, e ogni scrittura sulla sessione e' condizionata: un rate o un buffer gia' corretto non viene riscritto.
3. **Apri e riapri.** `setAudioChannels` apriva il device, e subito dopo `setAudioDeviceSetup` lo chiudeva e riapriva a 48 kHz / 256 comunque. Ora la sessione e' gia' a posto quando il device si apre, quindi l'apertura cade sul clock giusto e `applyAudioSetup` non ha piu' niente da cambiare: JUCE confronta il setup che riceve con quello su cui e' e torna senza toccare il device. Le uniche righe che possono ancora riaprirlo sono i canali d'ingresso, e vengono scritte solo nell'unico caso che lo richiede — microfono concesso dopo che il device era gia' aperto senza.

Con CLOCK e BUFFER su **AUTO** (il default) l'app non scrive niente sull'hardware: si apre sul clock che il mixer sta gia' dando.

### Pagina IMPOSTAZIONI

Il tasto **SETUP** in alto a destra apre una pagina dedicata. In prima pagina restava roba che si decide una volta prima del concerto e mai durante, a un tocco di distanza dal trasporto: sono passate tutte di la'.

| Gruppo | Cosa |
|---|---|
| **CLOCK** | AUTO / 44.1k / 48k / 88.2k / 96k. AUTO segue l'interfaccia |
| **BUFFER** | AUTO / 64 / 128 / 256 / 512 |
| **INGRESSO** | MIXER o IPAD; ELAB. OFF/ON (modo Measurement di iOS contro modo Default) |
| **PROVE** | tema, CLICK TEST, pannello DEBUG |
| **STATO** | clock e buffer **veri**, latenza, canali, route, se altre app stanno suonando, motore AI |

Le scelte sono ricordate tra un avvio e l'altro (`PropertiesFile`), quindi il clock scelto e' gia' quello con cui il device viene aperto al lancio successivo.

Da provare sul device: X-Air a 48 kHz, Spotify in riproduzione, apertura dell'app — niente crack e la traccia continua; poi CLOCK 44.1k mentre suona, che invece **deve** far ripartire l'audio (e' l'unica cosa in tutta l'app che riconfigura l'hardware di proposito).

## Bug Spotify / BPM a 0 (18 agosto)

L’AI **c’era** e il modo **IPAD/SPEAKER è quello giusto** (il microfono sente il mix in stanza; iOS non dà il PCM di Spotify).

Non lockava perché:

1. `heldBpm` veniva azzerato se il picco mic era sotto 0.0055 — tipico iPad speaker → stesso mic
2. Il lock richiedeva `heldBpm > 50` **dopo** quell’azzeramento (chicken-and-egg)
3. Makeup analysis non partiva sotto 0.0055
4. BeatDecoder voleva `pBeat > 0.32`; BeatNet su mix reale spesso sta sotto, quindi `valid=0` e BPM `--`

Ora: BPM neurale visibile anche in ascolto; makeup da ~0.0005; lock SPEAKER più permissivo; gate BeatNet 0.40 sull’attivazione combinata beat/downbeat; confidenza stabile tra un beat e l’altro.

UI: riga **AI ONNX | IPAD | nn | pBeat | valid**. Pannello DEBUG (in alto a destra) con onnx / source / mic vs analysis peak.

## START bloccato su ATTENDO BATTUTA + BPM che salta (18 agosto, pomeriggio)

Due bug dopo il lock SPEAKER più permissivo:

1. `pBeat`/`pDownbeat` sono probabilità **per frame**, non eventi. Ogni callback sembrava un beat/downbeat → `snapDownbeat` riportava la battuta a 1 e il clock non chiudeva mai la battuta → START restava su **ATTENDO BATTUTA**.
2. Con FOLLOWING, il target BPM inseguiva ogni stima neurale (e `lostSync` veniva azzerato ogni blocco).

Ora: solo i picchi del decoder contano; niente snap/griglia mentre si aspetta il 1; dopo ~2 bar o 3 s si parte sul prossimo quarto; BPM locked non rincorre salti > ~4.5% finché il disaccordo non è sostenuto.

## BPM Spotify fisso ma oscillante (18 agosto, pomeriggio)

`AI ONNX` confermava solo il caricamento della rete, non che il decoder fosse equivalente a BeatNet. Il problema era reale e riproducibile con il file ufficiale BeatNet `808kick120bpm.mp3`:

- le feature locali usavano 16 bande/ottava; BeatNet usa 24 bande/ottava sulla griglia madmom, con filtri duplicati rimossi fino a 136 bande;
- `diff_ratio=0.5` era interpretato erroneamente come `current - 0.5 * previous`; madmom calcola `current - previous`;
- il decoder guardava solo `pBeat`, ma il decoder ufficiale usa `max(pBeat, pDownbeat)` perché il downbeat è anche un beat;
- una curva di attivazione larga veniva contata più volte;
- la mediana di 8 IOI adiacenti poteva seguire sincopi/ottavi invece del periodo fondamentale.

Correzioni: filterbank allineata a madmom, differenza corretta, massimi locali causali, gate combinato 0.40 e consenso su 32 IOI includendo somme di 1–4 intervalli.

Evidenza diagnostica sul file ufficiale a 120 BPM:

- prima: `pBeat max 0.114`, nessun tempo valido;
- dopo feature corrette: `pBeat max 0.681`, `pDownbeat max 0.970`, **120.0 BPM stabile**;
- test host finale: **30 passed, 0 failed**.

Nota onesta: non è ancora incluso il particle filter Monte Carlo completo del progetto Python BeatNet. La rete ONNX è quella ufficiale; il decoder C++ è causale e stabilizzato per mobile.

## BPM al doppio e instabile (19 agosto)

Il sintomo — BPM riconosciuto al doppio, e comunque mai fermo — era reale e riproducibile. Misurato end to end su 120 brani sintetici (60–176 BPM, quattro stili, percorso SPEAKER con simulazione cassa→stanza→microfono):

| | prima | dopo |
|---|---|---|
| Ottava sbagliata | 41/120 | **25–26/120** |
| BPM instabile (span > 4 BPM nello stesso brano) | 39/120 | **24–29/120** |
| Cambi di livello metrico (52 tracce di attivazione) | 1590 | **15** |
| Test host | 36 | **42** (6 nuovi, ognuno verificato che fallisce sul codice vecchio) |

I due valori «dopo» sono due esecuzioni della **stessa** build: la sonda pilota il motore più veloce del tempo reale e il worker neurale gira su un altro thread, quindi lo scheduling sposta qualche caso da un run all'altro. Vanno letti come ordine di grandezza, non come cifre esatte. Le due colonne sono un run ciascuna.

Cause, tutte verificate con misure e non per ipotesi:

1. **L'autocorrelazione non sa scegliere l'ottava.** Sulle attivazioni BeatNet reali la correlazione a 1, 2, 3 e 4 battiti sta fra 0.78 e 0.97 per tutte: un treno di battiti correla con sé stesso a qualsiasi multiplo. A decidere il livello restava quindi il *prior* gaussiano su 120 BPM, che regala al doppio un vantaggio del 46 % a 60 BPM — cioè esattamente «lento → doppio, veloce → metà».
2. **Il livello si decide sull'ampiezza, non sulla correlazione.** Ripiegando l'attivazione sul periodo candidato, l'altezza a mezzo periodo dal picco vale ~0.13 sul battito vero e ~0.80 sul doppio. È una decisione, non un pareggio.
3. **Il livello ballava fra un refresh e l'altro.** Ora cambia solo se un rivale vince di un margine e lo mantiene per ~2 s.
4. **Il decoder non seguiva la correzione.** Il re-anchor era condizionato alla *clarity* del comb — che è bassa proprio quando la griglia sbagliata è il rivale — e vietato del tutto finché la griglia «stava bene», cosa sempre vera per il doppio del tempo giusto.
5. **Il livello di analisi era parte del problema.** Le feature sono `log10(mag + 1)`, quindi il guadagno conta. Il target era 0.12; l'ottimo misurato è **0.20**. E il guadagno inseguiva il picco istantaneamente, spostando il punto di lavoro della rete a ogni colpo di batteria.

## Tempo che oscilla su Spotify e battuta che riparte (19 agosto, sera)

I tre sintomi riportati provando con Spotify — **riconoscimento lento**, **BPM
che oscilla anche su un brano registrato in studio**, e **la battuta che fa
«uno, due» e torna all'uno** — erano tutti reali e tutti riproducibili. Col
click funzionavano perché un click non ha niente di quello che li causa.

### Come sono stati misurati

Serviva materiale che si comportasse come un disco, non come un segnale di
prova. `scripts/probe_song.cpp` (target `VPProbe`) genera trenta brani a tempo
**perfettamente fisso** — cinque arrangiamenti × sei tempi — con basso tenuto,
pad senza attacco, hi-hat sugli ottavi, sincopi, fill ogni otto battute e uno
stacco di quattro battute senza batteria; poi li fa passare per **cassa
dell'iPad → stanza → microfono dell'iPad**, taglio sotto i 250 Hz compreso: la
cassa dell'iPad la fondamentale del kick non la riproduce, quindi alla rete non
arriva. Sopra ci gira il motore intero, BeatNet ONNX compreso.

`scripts/probe_activations.cpp` registra le attivazioni della rete su quegli
stessi brani e `scripts/probe_replay.cpp` le rigioca dentro al solo
`BeatDecoder`: stesso segnale, stesse misure, un secondo invece di due minuti,
che è ciò che ha reso possibile tarare le decisioni invece di indovinarle.

| Su 30 brani a tempo fisso | prima | dopo |
|---|---|---|
| Battute che ripartono prima del quattro | 268 / 254 / 260 | **0 / 0 / 1** |
| Brani col BPM instabile (span > 1.5 BPM a regime) | 28 / 28 / 29 | **15 / 17 / 18** |
| Span medio del BPM a regime | 21.8 / 24.0 / 25.7 BPM | **10.8 / 11.3 / 13.0 BPM** |
| Salti di BPM > 1 | 312 / 301 / 334 | **125 / 150 / 156** |
| Brani lenti a stabilizzarsi (> 12 s) | 28 / 28 / 28 | **15 / 17 / 19** |
| Ottava sbagliata | 4 / 4 / 4 | 4 / 5 / 5 |
| Tempo fino a FOLLOWING | 2.3 s | 3.9 s |

Tre esecuzioni per colonna: la sonda guida il motore più veloce del tempo reale
e il worker neurale sta su un altro thread, quindi lo scheduling sposta qualche
caso da un run all'altro. Vanno lette come ordine di grandezza.

### Cause, tutte misurate

1. **Il livello metrico veniva scelto sui primi tre secondi.** Ripiegando
   l'attivazione BeatNet di un mix a 104 BPM sul periodo vero, il punto a mezzo
   battito vale **0.86** del battito nelle prime tre battute e **0.15** dopo
   dieci secondi: la LSTM si sta ancora scaldando e tre periodi sono troppo
   pochi per separare un bin sul battito da uno sul controtempo. In quella
   finestra il tempo vero viene *addebitato* di essere un'ottava troppo lento e
   vince il doppio. Ora il fold chiede cinque periodi (`kMinPeriods`), e niente
   è un livello «deciso» prima di nove secondi di buffer.
2. **L'isteresi difendeva il livello sbagliato fino a diciotto secondi.** Il
   seggio si cambiava solo dopo dieci refresh consecutivi, qualunque fosse il
   distacco — e siccome la *salience* riportata è il punteggio di chi siede,
   scendeva a 0.06 mentre il rivale stava a 1.00. Sotto la soglia di salience il
   decoder ignora del tutto il fold, quindi non vedeva nemmeno il disaccordo.
   Ora l'attesa dipende dal distacco: un rivale che stravince entra in due
   refresh. **Solo finché il livello è provvisorio**: una volta deciso serve
   l'attesa piena, altrimenti su materiale ambiguo i due livelli se lo passano
   ogni due secondi.
3. **La battuta veniva riportata all'uno dall'ultimo downbeat arrivato.** La
   rete mette sull'uno vero solo una *pluralità* dei suoi downbeat: ogni
   downbeat sbagliato faceva ripartire la battuta a metà. Ed era uno **snap di
   fase**, che buttava via lo stato dell'anello — errore di fase, trim misurato
   — per una correzione che riguardava solo il conteggio. Ora la battuta si
   corregge dallo **stesso voto** già usato per entrare, con maggioranza netta e
   una pausa di quattro battute fra una correzione e l'altra, e **ruota
   l'indice** invece di spostare la fase: non muove niente e non perde un
   impulso.
4. **Un tempo fisso non veniva rifinito, veniva inseguito.** Il regime si
   decideva su otto battute *consecutive* d'accordo fra il fit a 8 e quello a
   24: col microfono in stanza una battuta storta è normale e un contatore che
   qualunque di esse azzera non arriva mai in fondo, quindi il decoder restava
   in regime «vivo» a inseguire il fit a otto battute. Ora si decide sullo
   **spread del fit lungo** su una finestra di battute (una battuta storta
   allarga lo spread e poi esce), e una volta fisso il tempo converge verso la
   **media corrente** del fit lungo — con una banda morta di 0.05 BPM. È la
   differenza fra «rifinire» e «inseguire»: sulle attivazioni sintetiche lo
   span a regime è **0.00 BPM** su nove tempi da 62 a 186.
5. **La via d'uscita rapida dal tempo fisso scattava sul rumore.** Era la
   mediana di tre intervalli contro il tempo tenuto, tre volte nella stessa
   direzione: il jitter d'attacco di un mix in stanza la supera parecchie volte
   al minuto. Ora una deviazione piccola conta solo se la finestra da 24 battute
   pende dalla stessa parte, mentre una grande sta in piedi da sola (un cambio a
   gradino la finestra non può confermarlo: appena la griglia è sbagliata il
   gate on-grid smette di ammettere battute). Il gradino 120 → 132 resta
   agganciato in **3.6 s**, l'accelerando 120 → 140 resta entro **0.085 di
   battito** dalla pulsazione.
6. **Fra il 3% e un quarto d'ottava non era compito di nessuno.** Un click a 78
   BPM veniva seguito a 90 e ci restava: `log2(90/78)` è 0.21, sotto la soglia
   d'ottava, quindi il ri-aggancio non scattava mai — e il fit da solo non ne
   esce, perché su una griglia sbagliata del 15% sta interpolando quelle battute
   che per caso cadono dentro la tolleranza. Ora un fold deciso *tira* il tempo
   commesso verso di sé senza buttare via niente. Lo stesso click ora sta a
   **78.00 BPM, ±1 ms dalla pulsazione**.

### Cosa resta storto

- **Sotto ~92 BPM con ottavi pieni raddoppia ancora.** Sui brani di prova a 76
  BPM la correlazione a mezzo battito vale 0.73–0.77 contro 0.02–0.18 a 104 e
  128: gli ottavi sono forti quanto i battiti e 152 è una lettura difendibile.
  Ho provato a spostare la banda «troppo veloce» per farlo cadere dalla parte
  giusta: peggiora, perché la stessa asimmetria è ciò che impedisce di leggere
  in half-time un normale backbeat rock. Misurato, non ipotizzato.

  Non essendo decidibile dal segnale, ora **lo decide chi ascolta**: i due tasti
  **÷2** e **×2** accanto al BPM spostano il livello di un'ottava. Non è un
  numero cosmetico — l'ottava scelta viene applicata alla risposta del *fold*,
  quindi il decoder aggancia davvero quel livello, i picchi in mezzo cadono
  fuori griglia e vengono scartati come controtempi, e fit, fase e battuta
  lavorano lì senza sapere niente della scelta. Il test `octave-control`
  verifica la parte che conta: che ci **resti**, mentre il fold continua a
  nominare l'altro livello.
- Il half-time a 104 continua a discutere fra 104 e 52. È la stessa ambiguità
  vista da vicino: con rullante solo sul tre, 52 *è* una lettura in half-time.
- Il tempo fino a FOLLOWING è passato da 2.3 a 3.9 s, e su un click lento
  arriva a ~8 s. È il prezzo dichiarato del punto 1: prima si agganciava in due
  secondi all'ottava sbagliata e ci metteva mezzo minuto a uscirne.

### Test aggiunti

- `bar-integrity` — una rete finta sbaglia il downbeat una battuta su due, come
  fa quella vera; la battuta deve arrivare al quattro prima di ripartire. Sul
  codice vecchio: 12 ripartenze anticipate.
- `fixed-hold` — tempo fisso con 22 ms di jitter d'attacco, una battuta su
  dodici mancata e un fill ogni otto battute. Vecchio: span 1.67 BPM, regime
  tenuto il 61% del tempo. Nuovo: **0.64 BPM, 85%**.
- Il conteggio dei cambi di livello ora parte da quando l'estimatore dichiara il
  livello **deciso**, non dalla prima lettura: rivedere un livello provvisorio è
  il comportamento voluto, non un difetto.

## Shaker e congas interrotti (19 agosto)

Cinque difetti distinti, tutti nel percorso di riproduzione:

1. Ogni campione sintetizzato era un decadimento **troncato**: shaker al 21 % del picco, conga aperta all'11 %, slap all'8 %. Un click a **ogni** colpo. Ora ogni campione ha una dissolvenza finale di 12 ms.
2. Una voce sostituita dal colpo successivo veniva spenta a metà campione — ora sfuma in 4 ms.
3. Con tutte le voci occupate l'allocazione ripiegava sullo **slot 0**, sovrascrivendo una voce in suono. Ora prende la più vecchia; le voci sono 16.
4. La guardia anti-ritrigger era l'82 % di un impulso calcolato sul BPM **visualizzato** (120 finché non c'è lock): sopra ~145 BPM reali era più lunga dell'impulso vero e **buttava via un colpo su due**. Ora è una guardia fissa di 20 ms, e il groove prende il tempo dal clock.
5. `pulseBeatInBar` era sbagliato per gli impulsi dopo un confine di battito dentro lo stesso blocco, e il tumbao sceglie la conga da quell'indice: suonava il tamburo sbagliato.

## Limiti noti dopo questo giro

- Sopra ~168 BPM e sui groove in **half-time** il tracker preferisce il livello lento (84 invece di 168). Per un orologio di percussioni è spesso la lettura più utile, ma è una scelta, non una certezza.
- Sotto ~92 BPM con ottavi pieni può ancora raddoppiare — vedi la sezione del 19 agosto sera, dove è misurato e spiegato perché non si corregge spostando le soglie.
- Restano numeri su materiale **sintetico**, per quanto il percorso cassa → stanza → microfono sia simulato: la prova sul device con Spotify vero è ancora da fare.
- Quasi tutto quello che sopra è dato per storto, è storto **in modalità IPAD**. In MIXER lo span medio è 7,8 BPM contro 27,8 e la battuta è allineata 10 volte su 12 contro 5. Se il timing conta, MIXER.

## Cosa locka oggi

Il modello è BeatNet addestrato su **musica** (GTZAN), non sul click sintetico.

| Sorgente | Atteso |
|---|---|
| Kit / groove con kick+snare (test host) | Lock automatico senza TAP |
| Spotify in SPEAKER (casse iPad, microfono) | Modo **IPAD**. Volume alto aiuta; aspetta FOLLOWING. CLICK TEST **spento** |
| CLICK TEST | Kick+snare+hat a 120. Può lockare; se no, TAP |
| Piano / ambient / rubato | Può restare in LISTENING o sbagliare l’ottava. TAP resta il fallback |

SPEAKER: l’app **non** legge l’audio digitale di Spotify. Sente il microfono.

Se la riga AI dice **AI STUB**, il `.onnx` non è nel binario: riconfigura iOS.

## Come provare Spotify

1. Modo **IPAD** (non MIXER)
2. CLICK TEST **off**
3. Spotify in play, volume alto
4. Controlla: **AI ONNX**, **MIC LIVE**, `nn` che si muove, poi **FOLLOWING**
5. **START** sul downbeat
6. Se `nn --` e `p 0.00` dopo 10 s: il mic non sente il mix (AEC / troppo lontano). Avvicina l’iPad o alza Spotify
7. TAP resta il fallback

## Installare sull’iPad

```bash
./scripts/setup-ai.sh
./scripts/configure-ios.sh
open build-ios/VirtualPercussionist.xcodeproj
```

In Xcode: team `28H5MJ7244`, iPad Air M1, Run, microfono. Primo avvio: CoreML può compilare il grafo qualche secondo.

## Runtime

- Worker (non audio thread): resample 22.05 kHz → BeatNet LSTM → decoder → PLL
- START aspetta il **primo quarto** (downbeat). BeatNet allinea la battuta 1.
- Shaker **monofonico** (un grain per 8ª, niente overlap a tempo stabile) + riff di **congas** (tumbao: tumba / open / slap)
- Follow strength: high. Griglia: ottavi
- CoreML su iPad; se l’EP rifiuta, ORT ricade sulla CPU
- Attribuzione: Heydari, Cwitkowitz, Duan — BeatNet, ISMIR 2021, CC BY 4.0

## I comandi sullo schermo

Dal 19 agosto l'interfaccia espone tutto quello che il motore sa fare. Prima
`grooveStyle`, `grooveAuto`, `congasEnabled`, `swing` e `intensity` esistevano nel
C++ ma non erano raggiungibili: l'app suonava sempre la marcha, e tre parti su
quattro erano intestabili.

| Comando | Cosa fa |
|---|---|
| **÷2 / ×2** | dimezza o raddoppia il livello metrico, quando l'analisi lo legge un'ottava fuori. Premere di nuovo lo stesso tasto torna a quello misurato |
| **SPOSTA L'1** | sposta la battuta di un movimento. Serve perché il primo quarto non è ricavabile in modo affidabile dal segnale — vedi la sezione del 20 agosto |
| **DARK / LIGHT** | tema. All'avvio segue l'impostazione di sistema; toccarlo lo fissa |
| **AUTO / MARCHA / ROCK / DANCE / POP** | la parte che suona. AUTO lascia scegliere allo `StyleDetector`; premere una parte a mano spegne AUTO |
| **SHAKER / CONGAS** | accendono e spengono i due strumenti separatamente |
| **SOURCE (IPAD / MIXER)** | microfono in stanza contro mic ravvicinato |
| **1/4 · 1/8 · 1/16** | griglia dello shaker |
| **SWING** | 0 = dritto, 1 = terzinato. Da alzare sugli shuffle |
| **ENERGIA** | quanto forte suona il percussionista |
| **REVERB** | ambiente |

La riga **PARTE** sotto il BPM dice sempre quale parte sta suonando; con AUTO
attivo mostra anche la confidenza del rilevatore e tinge di verde il pulsante che
ha scelto. Dal restyling della sera del 19 agosto **tutti** i comandi restano
visibili anche in orizzontale: vedi la sezione sull'interfaccia più sotto.

## Lo shaker in ritardo, e il primo quarto (20 agosto)

Due richieste. La prima aveva una causa precisa e ora è risolta; la seconda non
si risolve con questo modello, e sotto c'è scritto perché — misurato, non
supposto.

### Misurare il suono, non l'orologio

Tutto quello che i test misuravano era il **clock**: la fase che riporta contro
il battito notato. Un ascoltatore il clock non lo sente. Fra l'impulso e
l'orecchio ci sono l'attacco del campione, l'offset nel blocco e il percorso
d'uscita — e un colpo può essere in ritardo di dodici millisecondi buoni con un
clock perfetto.

`scripts/probe_timing.cpp` (target `VPTiming`) misura l'audio che il motore
produce: per ogni battito notato cerca l'attacco nell'uscita e riporta di quanto
è distante. Con congas spente, shaker sui quarti, niente feel e niente riverbero,
ogni colpo dovrebbe cadere esattamente su un battito.

### Lo shaker era in ritardo perché lo shaker non è un click

| stroke | 20% | 50% | **80%** | picco |
|---|---|---|---|---|
| shakerDown | 0.6 | 3.3 | **13.3** | 14.4 |
| shakerUp | 0.1 | 2.1 | **10.0** | 11.1 |
| slap | 0.8 | 0.9 | **2.8** | 3.1 |
| heel / toe | 0.9 | 1.0 | **2.9** | 3.2 |

Millisecondi dal trigger. Lo shaker ha una **salita lenta**: la sua energia ci
mette dieci-tredici millisecondi ad arrivare dove lo slap arriva in tre. Una
voce fatta partire esattamente sull'impulso quindi *si sente* dopo — e i colpi
si sentono in momenti diversi fra loro, che è peggio che essere tutti in ritardo
insieme.

Due correzioni:

1. **Ogni articolazione viene trattenuta** della differenza fra l'attacco più
   lento della banca e il proprio, così slap e shaker scritti sullo stesso
   sedicesimo si *sentono* insieme. L'attacco è misurato dai campioni al momento
   della costruzione della banca, non scritto a mano: cambiare la libreria
   cambia i numeri da sola.
2. **Il clock anticipa di altrettanto.** È la stessa specie di ritardo del
   round-trip del dispositivo — tempo fra la decisione e il suono — e
   `VirtualPercussionEngine` è l'unico punto che conosce entrambi.

| Su 12 brani | prima | dopo |
|---|---|---|
| Attacco **sentito** rispetto al battito | **+16.3 ms** | **+3.1 ms** |
| Distanza fra le articolazioni | 2.1 – 13.3 ms | **0.4 ms** |

Un tranello, che vale la pena aver scritto: compensare ogni *presa* con il
proprio attacco ha fatto crollare la differenza fra due colpi consecutivi da
1.42 a **0.001**. Diversi slot del round-robin sono la stessa registrazione con
l'inizio spostato, e quello spostamento *è* la variazione; misurarlo e
toglierlo l'aveva annullata. La compensazione appartiene all'**articolazione**,
non alla presa.

Nota: `TempoFollower::setLatencyCompensationMs` scriveva due campi che nessuno
leggeva — codice morto. La latenza d'uscita era ed è compensata altrove, dentro
`leadMs`.

### Il primo quarto: quattro strade, tutte al livello del caso

Questa non è risolta, e non per mancanza di tentativi. Misurato su 12 brani,
quante volte la battuta dell'orologio coincide con quella del brano:

| Approccio | Battuta allineata | Entra sull'uno |
|---|---|---|
| Voti della rete, conteggio semplice (prima) | 5/12 | 1/12 |
| Voti **pesati** per la confidenza, con decadimento | 6/12 | 4/12 |
| + template metrico (rullante su 2 e 4, kick più pesante su 1) | 5/12 | 2/12 |
| + novità spettrale (l'accordo cambia sull'uno) | 5/12 | 2/12 |

Il caso puro sarebbe 3/12. Perché nessuna funziona:

1. **L'uscita downbeat della rete non identifica la battuta.** Ripiegando
   `pDownbeat` sulla battuta, i picchi cadono ogni **singolo movimento**, non
   uno ogni quattro, e il massimo sta sul terzo movimento tanto spesso quanto
   sul primo. Votare su quello è votare sul rumore.
2. **Il template metrico sbagliava di esattamente un movimento.** Il corpo di un
   rullante sta sui duecento hertz, cioè *dentro* la banda che il template
   leggeva come kick: le due prove concordavano fra loro e indicavano entrambe
   il movimento due. Qualunque regola scritta come «quale strumento sta dove»
   dipende da come gli strumenti si dividono le bande.
3. **La novità spettrale è rumore** su questo materiale.

Quello che resta, ed è tenuto perché è comunque il modo giusto di usare
l'informazione: i voti sono **pesati per la confidenza** invece che contati,
decadono, e ruotano insieme alla battuta invece di essere buttati quando la
battuta si muove. E muovere la battuta *mentre si suona* ora richiede molta più
evidenza: con una risposta giusta quattro volte su dieci, una battuta
costantemente sbagliata si corregge con un tocco, una che continua a spostarsi
no.

**Il tocco è il tasto `SPOSTA L'1`**, accanto ai pallini. Stessa risposta dei
tasti d'ottava: una misura che non viene fuori si offre a chi ascolta invece di
tirarla a indovinare.

> **Aggiunta del 20 agosto.** Tutte le misure di questa sezione sono state fatte
> in modalità IPAD. Rifatte in MIXER la battuta è allineata **10 volte su 12** e
> la percussione entra sull'uno **7 volte su 12**. Non è il rilevamento del
> downbeat a non funzionare: è il percorso cassa → stanza → microfono che lo
> distrugge. Vedi «MIXER e IPAD sono due prodotti diversi».

### Il materiale di prova aveva un difetto

I brani della sonda avevano il kick identico su 1 e 3, il rullante identico su 2
e 4, e il basso che si articolava **ogni due movimenti**: la periodicità più
forte sopra il battito era di due, non di quattro. Il materiale non aveva una
battuta, e misurare l'uno contro un riferimento arbitrario non misura niente.
Ora il basso si articola sulla battuta, il kick sull'uno è un po' più pesante e
c'è un piatto sull'uno ogni quattro battute — come fanno i dischi.

È materiale più difficile per il tempo: lo span medio a regime passa da ~11 a
~20 BPM (con una varianza fra esecuzioni di ±5). Verificato che dipende dal
materiale e non dal codice, spegnendo la compensazione e rimisurando: **con** la
compensazione lo span è 17.5, **senza** 26.4, quindi la compensazione lo
migliora.

## Riff lunghi e il tap che dichiara l'uno (20 agosto)

### La parte era una cella su ripetizione

Due difetti, tutti e due invisibili leggendo il codice e ovvi misurandolo.

Lo **shaker** era un movimento di pesi scritto quattro volte. Sopra ci stava un
accento per movimento, che scala un intero movimento alla volta: bastava a far
sembrare i quattro movimenti diversi fra loro all'ascolto distratto, e bastava a
far passare un test scritto sui pesi grezzi. Normalizzando ogni movimento per il
proprio picco — cioè togliendo l'accento e guardando la *figura* — la differenza
fra un movimento e il successivo era esattamente **0.00**.

Le **congas** alternavano due battute per sempre: A B A B. Una frase di due
battute non è una frase.

Ora la tabella dello shaker è lunga una battuta intera per ognuno dei quattro
stili, e la frase è di quattro battute, **A B A C**, con una terza battuta
propria per stile. Misurato con l'accento tolto:

| Stile | movimento vs movimento | prima metà vs seconda |
|---|---|---|
| MARCHA | 0.51 | 0.41 |
| ROCK | 0.17 | 0.36 |
| DANCE | 0.51 | 0.36 |
| POP | 0.46 | 0.31 |

Con la tabella vecchia la prima colonna è 0.00 e il test fallisce in tutti e
quattro gli stili; l'alternanza a due battute lo fa fallire allo stesso modo. Il
fill sulla quarta battuta resta dov'era.

### Il tap sull'uno

Dato che l'uno automatico è al livello del caso (vedi sopra), il primo tocco di
una serie di TAP, quando un tempo c'è già ed è tenuto, **dichiara che quel
momento è il primo quarto**. Non tocca il tempo: quello resta della rete, e i
tap successivi si comportano come prima (dal quarto in poi prendono il tempo).
Insieme alla dichiarazione i voti della rete vengono azzerati e l'allineamento
automatico è tenuto fermo per trenta secondi, altrimenti la battuta torna dov'era
entro una frase e la correzione sembra non aver fatto niente.

Sui pallini, l'uno porta un anello per nove decimi di secondo dopo il tocco: una
battuta è una cosa lenta da vedere muoversi, e senza un segno il gesto sembra non
aver fatto nulla fino al downbeat successivo.

### Il test che non misurava niente

Vale la pena scriverlo perché è lo stesso errore di misura di due giri fa. La
prima versione del test sceglieva su quale movimento battere leggendo il
contatore dell'orologio **esattamente sul bordo del movimento** — dove il bordo
dell'orologio e quello del brano non coincidono (l'orologio anticipa della
compensazione d'attacco) e la lettura riporta il movimento di prima o quello
dopo a caso. Sceglieva così un movimento che l'orologio stava *già* chiamando
uno, e poi confrontava l'orologio con sé stesso: **96% con il meccanismo, 96%
senza**.

Ora ogni lettura è presa a metà movimento, lo scarto fra i due conteggi è
misurato prima e il movimento su cui battere è scelto da quello, un movimento in
anticipo. Risultato: **0% prima del tap, 100% dopo**, e **0% dopo** con la
dichiarazione tolta dal codice, su tre esecuzioni.

Il timing non è cambiato: attacco sentito +3.01 ms, identico a prima del riff.

## Il livello metrico, e il materiale che nessuno aveva mai provato (20 agosto)

Segnalazione: su una registrazione della band dal vivo, in modalita' iPad, il
tempo ancora non regge. Richiesta: la soluzione migliore che esiste.

### Due cose sbagliate, e una era nel banco di prova

**La prima.** Ogni misura in questo repository era su `ph += inc` con `inc`
costante: tempo perfettamente fisso, sequencer. La band di chi lo usa non e' un
sequencer. `SongOptions` ha ora `driftBpm` (deriva lenta, due sinusoidi a
periodi incommensurabili) e `jitterMs` (scarto umano per battito, tenuto per
tutto il battito - un batterista in anticipo lo e' per tutto il movimento, non
per ogni colpo). E la sonda misura una cosa nuova, l'**errore medio** dalla
curva di tempo che il brano suona davvero, perche' su materiale che si muove lo
`span` non vuol dire piu' niente.

La differenza e' grossa, e spiega perche' funzionava nei test e non a casa sua:

| IPAD | sequencer | band dal vivo |
|---|---|---|
| ottava sbagliata | 5/30 | **11/30** |
| instabili | 17/30 | **26/30** |
| salti > 1 BPM | 194 | **310** |
| errore medio | 21,5 | **29,7 BPM** |

**La seconda.** Il difetto dominante non era instabilita', era il **livello
metrico**: sotto i cento BPM il decoder leggeva gli ottavi come battiti e
suonava al doppio, con sicurezza e senza oscillare. Trenta attivazioni
registrate, ripassate nel decoder da solo: tutti e sei i brani a 76 danno 152,
tutti e sei quelli a 92 danno 184. Da 104 in su lo span sta sotto 2 BPM.

### Tre strade chiuse, misurate

Prima di cambiare l'algoritmo ho controllato se l'informazione sul livello ci
fosse e non venisse usata. Non c'e':

1. **Energia per bande** (260-600, 600-1.5k, 1.5k-4k, 4k-9k), ripiegata sul
   battito: il rapporto battito/ottavo e' 1,3-1,8 **a ogni tempo**, uguale a 128
   dove il tracker ci prende e a 76 dove sbaglia. Nessun segnale.
2. **Timbro** (corpo contro brillantezza all'attacco): separazione 0,67-0,73,
   di nuovo costante col tempo. Nessun segnale.
3. **La caratteristica che il fold gia' usa** — quanto e' alto il mezzo battito
   rispetto al battito. Misurata su tutte e trenta le tracce, contro il
   candidato *davvero* troppo lento: le due distribuzioni si **sovrappongono
   completamente** (troppo lento 0,51-1,00; livello vero 0,04-0,80). Nessuna
   soglia le separa. Questo chiude definitivamente la strada su cui erano stati
   spesi due giri.

La cassa dell'iPad taglia tutto sotto i 260 Hz: la cassa della batteria
sparisce, e con lei l'informazione che un percussionista usa per sapere quale
colpo e' il battito.

### La soluzione: lo spazio di stati (tempo, fase)

E' il modello del *bar pointer* — Whiteley/Cemgil/Godsill 2006, reso efficiente
da Krebs/Bock/Widmer 2015 — cioe' quello dietro al beat tracker DBN di madmom e
al filtro a particelle di BeatNet. Lo stato e' una coppia: a che tempo sta la
musica, e a che punto del battito. Ogni frame avanza di una posizione; alla fine
di un battito puo' cambiare tempo, e **cambiare tempo costa**.

Quel costo e' tutto il punto, ed e' quello che la macchina precedente non aveva.
Un filtro a pettine ricalcolato ogni otto frame risponde a «quale periodo spiega
meglio gli ultimi secondi» ed e' libero di rispondere diverso la volta dopo —
che e' esattamente il tempo che scatta fra una battuta e l'altra. Qui muovere il
tempo va pagato con l'evidenza, quindi si muove solo quando la musica si e'
mossa, e si muove come si muove un musicista: al tempo vicino, con continuita'.

Due dettagli che ho dovuto misurare per farli giusti:

- **La normalizzazione dell'osservazione.** Dividere la massa «qui non c'e' un
  battito» per il periodo fa pagare a un tempo lento ogni frame che passa fra i
  suoi battiti: lo spazio collassa sul tempo piu' veloce che contiene, e infatti
  la prima versione riportava 230 BPM su tutto. Va divisa per una costante
  (sedici, la scelta di madmom), cosi' ogni tempo passa la stessa *frazione* del
  suo tempo sul battito.
- **Il costo del cambio va preso in valore assoluto, non al quadrato.** Al
  quadrato un cambio di un passo costa un millesimo, cioe' niente, e il tracker
  vaga per tutto lo spazio. Con `|rel|` e lambda 200 sta fermo.

E un terzo che e' una scelta, non un dettaglio: il **prior percettivo**, centrato
a 118 BPM con larghezza 0,40 ottave. Non e' un pareggio deciso all'ultimo: fa
parte del sentire. Nessuno batte le mani a 184 su un pezzo lento, e il motivo
non e' che 184 non ci sta — e' che 184 non e' una pulsazione.

### L'innesto: il livello dallo spazio di stati, la precisione dal fold

I due falliscono in posti opposti — il fold legge gli ottavi come battito sotto
i cento, lo spazio di stati viene tirato verso il centro della gamma agli
estremi. Quindi lo spazio di stati fornisce **solo l'ottava**, con isteresi, e
tutto il resto della macchina resta com'era: il fit ai minimi quadrati risolve
il tempo molto piu' fine di quanto possa uno spazio di stati a periodi interi.

Sulle stesse trenta attivazioni registrate, decoder da solo:

| | prima | dopo |
|---|---|---|
| **span medio** | 15,35 BPM | **0,55 BPM** |
| ottava sbagliata | 7/30 | 5/30 |
| instabili | 12/30 | 9/30 |
| tempo per arrivare entro il 2% | 6,5 s | **3,8 s** |

Motore intero, IPAD, band dal vivo — il caso segnalato:

| | prima | dopo |
|---|---|---|
| ottava sbagliata | 11/30 | **5/30** |
| **errore medio** | 29,7 BPM | **13,7 BPM** |
| span medio | 13,5 | **7,5 BPM** |
| salti > 1 BPM | 310 | 296 |

MIXER non peggiora: ottava 1/30 come prima, errore medio 2,89 BPM. La larghezza
del prior e' stata scelta su quel vincolo — a 0,55 il guadagno su iPad era lo
stesso ma tre brani a 140 in MIXER venivano tirati a 70.

Costo: **niente di misurabile**. Millesettecento stati a cinquanta frame al
secondo sul thread di analisi; l'app resta al 2,0% di un core e il callback allo
0,38% del suo budget.

### Cosa resta storto, e perche'

I cinque che restano sono tutti a **76 BPM letti 152**. A quel tempo, con gli
ottavi pieni, il prior a 118 preferisce 152 — e onestamente: 76 e' al limite di
quello che una persona batte, e su quella curva 152 e' una lettura difendibile.
Spostare il centro del prior piu' in basso e' stato provato (95, 105, 112) e
peggiora il totale. Resta il tasto **÷2**.

Misurato anche l'inviluppo dentro cui il modello tiene il livello giusto: ottavi
fino a circa il 75% del battito con picchi larghi un frame e mezzo, fino al 55%
con picchi larghi tre e mezzo. Oltre raddoppia, e a quel punto la curva
*davvero* somiglia a un battito ogni ottavo.

## MIXER e IPAD sono due prodotti diversi (20 agosto)

Fino a qui **ogni** sonda girava in modalita' IPAD: cassa dell'iPad, stanza,
microfono. La modalita' MIXER — una mandata di linea, senza niente davanti — era
coperta solo dai test che non impostano la sorgente e quindi ereditano il valore
di default. Adesso `VPProbe` e `VPTiming` prendono `--ipad` / `--mixer` e sono
state misurate tutte e due.

### Il TAP non prendeva il tempo in MIXER

Difetto vero, e c'era dal primo commit:

```cpp
const bool tapOwnsTempo = tapEstablished && speakerFollow && heldBpm > 50.0f;
```

Il TAP e' lo stesso tasto nelle due modalita' e chi lo preme sta dicendo la
stessa cosa in tutte e due: il tempo lo so io meglio dell'analisi. Ma era
onorato solo mentre si seguiva la cassa dell'iPad — il caso attorno a cui il
flusso del TAP era stato pensato. Su una mandata di mixer quattro tocchi
mettevano il tempo e la rete se lo riprendeva subito.

Misurato su un brano a 100 BPM, con quattro tocchi a 132:

| | tiene il tempo battuto |
|---|---|
| IPAD | 100% del tempo |
| MIXER | **0%** |

Tolto il vincolo, tutte e due al 100%.

### E poi: in MIXER il tracker e' un'altra cosa

Trenta brani a tempo fisso, stesso materiale, cambia solo cosa c'e' davanti al
microfono:

| | MIXER | IPAD |
|---|---|---|
| span medio a regime | **7,8 BPM** | 27,8 BPM |
| instabili (>1,5) | 9/30 | 21/30 |
| lenti (>12 s) | 6/30 | 19/30 |
| ottava sbagliata | 1/30 | 4/30 |
| salti > 1 BPM | 67 | 244 |
| t_lock medio | 5,8 s | 4,6 s |

E il timing, dodici brani:

| | MIXER | IPAD |
|---|---|---|
| attacco sentito | **+1,41 ms** | +3,13 ms |
| errore medio dell'orologio | **-0,56 ms** | +12,70 ms |
| battuta allineata | **10/12** (70% del tempo) | 5/12 (38%) |
| entra sull'uno | **7/12** | 2/12 |

Su materiale dritto e sincopato, in MIXER lo span sta sotto 1,5 BPM quasi
ovunque e l'aggancio arriva in 5-7 secondi. Quello che resta storto sono i pad a
76 e 140, e half-time a 128 e 140 — cioe' gli stessi casi limite gia' descritti
sopra, non un problema di modalita'.

**Questo cambia una conclusione precedente.** L'uno automatico era stato
dichiarato "al livello del caso", e lo e' — ma quella misura era stata fatta
tutta in IPAD. In MIXER la battuta e' allineata 10 volte su 12 e la percussione
entra sull'uno 7 volte su 12. Non e' il rilevamento del downbeat a non
funzionare: e' il percorso acustico che lo distrugge. Il tasto `SPOSTA L'1` e il
tap sull'uno restano quello che servono in IPAD; in MIXER servono molto meno.

Stessa storia per l'orologio: in MIXER l'errore residuo e' **mezzo
millisecondo**, in IPAD dodici. Dodici millisecondi sono il ritardo che la
stanza aggiunge all'attacco che la rete sente, e l'app non ha modo di
conoscerlo. Si potrebbe aggiungere un anticipo fisso in modalita' IPAD e i
numeri della sonda migliorerebbero — ma sarebbe una costante tarata su una
*simulazione* di stanza che nessuno ha ancora confrontato con un iPad vero in
una stanza vera, cioe' esattamente il tipo di numero che smette di essere vero
senza dirlo. Non l'ho fatto.

**In pratica: se il timing conta, MIXER.** IPAD resta la modalita' comoda, e il
limite li' e' il segnale, non il tracker.

### La battuta quando l'analisi perde audio: trovata, non risolta

Sotto ThreadSanitizer il worker resta cosi' indietro che la FIFO va in overrun
per davvero, ed e' l'unica condizione in cui ho visto la battuta ripartire prima
del quarto movimento — cioe' il "uno, due, uno" di cui si lamentava l'utente.
Non e' una condizione che un dispositivo raggiunge: la sonda a 30 brani segna
zero buchi e la FIFO tiene undici secondi.

Serviva un modo di arrivarci senza sanitizer, e adesso c'e': `bar-starved`
alimenta il motore piu' in fretta di quanto il worker riesca a consumare.
Centoquaranta buchi in un minuto di analisi, con la battuta ancora osservabile.

Ho provato la correzione ovvia — dimezzare i voti del downbeat a ogni buco, cosi'
che la battuta non venga spostata su prove raccolte *prima* del buco e usate
*dopo* — e **non funziona**. Con e senza, su una dozzina di esecuzioni, le
ripartenze anticipate stanno fra zero e una in entrambi i casi. Non ho tenuto la
modifica: una patch non dimostrata nella logica piu' delicata del tracker e'
esattamente il genere di cosa che poi peggiora qualcos'altro in silenzio.

Quindi il test asserisce quello che la misura sostiene, non di piu': che il
percorso e' raggiungibile, e che le ripartenze restano rare (soglia due; dieci
farebbero fallire). Il difetto resta aperto e scritto qui.

### Cosa ho controllato e lasciato com'era

Il recupero dal falso aggancio (`speakerFollow && ! armed && ...`) e' anch'esso
solo in modalita' cassa. Ho aggiunto un test che prova le due modalita' con del
rumore di fondo prima di START, a quattro livelli diversi attorno alle soglie:
LOCKING 0% del tempo in tutte e otto le combinazioni. Non ho evidenza che quel
vincolo faccia danno, quindi non l'ho toccato — il test resta come guardia.

Le soglie di rumorosita' sono diverse fra le due (`0,0010` contro `0,0020`, e
`0,004` contro `0,020`) ed e' voluto: un microfono che sente una cassa a mezzo
metro parte molto piu' basso di una mandata di linea.

## Revisione del core: affidabilita' e prestazioni (20 agosto)

Un giro fatto con gli strumenti invece che leggendo: sanitizer sull'intera
suite, e una sonda che misura quanto costa davvero il callback audio.

### Prima cosa: le prestazioni non erano il problema

`VPCpu` misura il callback contro il suo budget e la CPU totale contro i
secondi di audio.

| | |
|---|---|
| Callback (buffer 256, budget 5333 us) | media **18,8 us = 0,35%** del budget |
| | p95 32 us, p99 51 us, peggiore 149 us (2,8%) |
| App intera, callback piu' worker | **2,0% di un core** per secondo di audio |

Con questi numeri ottimizzare il ciclo delle voci o l'autocorrelazione sarebbe
stato lavoro speso dove non serve, e rischio di regressione in cambio di niente.
Quindi non e' stato fatto. L'unica cosa che costava davvero e' finita in fondo a
questa sezione, ed era il numero di risvegli del thread, non l'aritmetica.

### La FIFO fra thread audio e worker aveva una corsa vera

`push` (thread audio) e `pop` (worker) scrivevano **entrambi** il puntatore di
lettura. Quando il produttore sorpassa il consumatore e sposta avanti quel
puntatore, il consumatore puo' scriverci sopra una posizione gia' superata: la
lettura torna indietro e gli stessi campioni vengono consegnati due volte, con
il conteggio di quelli persi sbagliato di conseguenza — e da quel conteggio
dipende ogni timestamp a valle, cioe' l'anticipo con cui l'orologio suona.

Non e' teoria. Un test con i due thread a tutta velocita' su un buffer piccolo
apposta, prima:

    fifo-race  letti 396608 di 400000, persi 296575, all'indietro 288, misti 69

Ora il puntatore di lettura appartiene al solo consumatore: il produttore scrive
e tira dritto, il consumatore si accorge del sorpasso, salta il buco e lo mette
in conto. Dopo la copia verifica di non essere stato sorpassato *durante*, e in
quel caso la butta invece di consegnare un blocco fatto di due momenti diversi.
Con lo stesso test, quattro esecuzioni:

    all'indietro 0, misti 0   (continuando a passare dal percorso di overrun)

Le celle stesse sono lette e scritte tramite `std::atomic_ref`: un anello che
sovrascrive puo' sempre avere il produttore dentro la zona che il consumatore
sta copiando, ed e' quello che la verifica serve a intercettare — ma dev'essere
un accesso concorrente **definito**, non una corsa che il compilatore ha il
diritto di dare per impossibile. ThreadSanitizer sulla suite intera: da 1
segnalazione a **0**.

Il margine di risincronizzazione e' dimensionato sulla lettura, non sul buffer.
Una prima versione lasciava mezzo buffer di margine: sono **5,5 secondi** di
audio buttati a ogni overrun. Per spoilare una copia il produttore dovrebbe
scrivere una lettura intera di campioni nel tempo che ci vuole a copiarne
altrettanti, cosa che non gli riesce nemmeno da lontano; secondi di audio
buttati invece sono un tracker peggiore di quello che si voleva evitare.

### Nota sul confronto con la sonda a 30 brani

Ho provato a validare questo giro con `VPProbe` e la prima lettura diceva
peggioramento (span medio da 17 a 24 BPM). Era rumore. Due cose lo dimostrano:

- lo **stesso** binario, sei esecuzioni, va da **15,6 a 24,4** BPM di span medio
  — il worker gira su un thread suo e quanto resta indietro lo decide lo
  scheduler dell'host, non il codice;
- la colonna `gaps` che ho aggiunto dice **0 su 30 brani**: la sonda non passa
  *mai* dal percorso di overrun, quindi tutto il lavoro sulla FIFO li' dentro e'
  inerte per definizione.

Lo stesso vale per il resto: i blocchi della sonda stanno sotto `maxBlock`
(niente divisione), l'ingresso e' sempre finito (niente guardia che scatti), il
worker non resta mai a corto di audio (niente attesa). L'unica differenza
davvero attiva e' il flush dei denormali, che sposta i bit dell'ultima cifra.

Quindi: **questo giro non tocca i numeri del tracking**, e va letto cosi'. La
sonda resta utile per il tempo, ma una singola esecuzione non e' un confronto —
e adesso la sua intestazione lo dice.

### Lo slot dell'ipotesi non era ordinato nel verso che serve

Il contatore di sequenza va dispari mentre si scrive e pari quando il dato e'
intero. Ma una `store` release impedisce solo alle scritture *precedenti* di
scavalcarla in avanti: non impedisce al dato di scavalcarla **all'indietro**,
passando davanti al marcatore dispari. Su una macchina che riordina le
scritture — cioe' ogni ARM, cioe' l'iPad — il lettore poteva copiare un dato a
meta' con il contatore ancora pari, e il suo stesso controllo gli avrebbe detto
che la copia era buona. Il risultato e' un bpm di un frame con la posizione
campione di un altro: un bersaglio di fase che punta nel posto sbagliato.

Ora ci sono barriere esplicite da entrambe le parti, e il dato e' tenuto come
parole atomiche invece che come struct normale, perche' due thread che toccano
lo stesso oggetto non atomico sono una corsa qualunque cosa dica il contatore.
Costa dieci copie di parola cinquanta volte al secondo.

Onesta' sulla verifica: su x86 l'hardware non riordina le scritture, quindi il
test che ho aggiunto **non puo'** fallire qui per quel motivo. Vale su ARM. Il
test verifica quello che vale ovunque, cioe' che il contatore validi la copia.

### Un blocco piu' lungo del previsto veniva troncato

`process()` tagliava a `maxBlock` e usciva. La coda del buffer di uscita
dell'host restava con dentro quello che c'era — a piena scala — e l'ingresso che
avrebbe dovuto analizzare spariva. Un host ha il diritto di consegnare un blocco
piu' lungo di quello che ha annunciato: cambio di rotta audio, blocco schermo,
AirPlay. Ora viene diviso. Test: 1061 campioni chiesti con buffer preparato a
256, 0 lasciati intatti.

### Un campione non finito dal microfono avvelenava l'analisi per sempre

Un solo infinito entrava nell'inviluppo di livello, che ha un rilascio di
quattro secondi: infinito meno infinito e' un NaN da cui non esce piu', e con
lui il guadagno a cui la rete viene alimentata. Peggio, un picco preso con
`std::max` **nasconde** il problema invece di trovarlo — `max(x, NaN)` e' `x` —
quindi il campione cattivo passava dritto nell'analisi mentre tutti i misuratori
segnavano normale.

Ora i campioni non finiti diventano silenzio e vengono contati (`badInputSamples`
nello snapshot, insieme al guadagno dell'analisi). Con la guardia il guadagno
resta a 9,88 dopo il buffer cattivo; senza, 1,07.

### Denormali

Nessun `ScopedNoDenormals` nel callback. Ogni filtro qui dentro ha una coda che
scende verso zero — il cancellatore del rientro dalle casse, l'inviluppo di
livello, il riverbero — e un float che finisce nell'intervallo denormale costa
cento volte uno normale su certi core. Il silenzio dopo un pezzo forte e'
esattamente quando succede, e un callback che sfora il budget e' un buco.

### Numeri letti da un thread mentre un altro li scriveva

Le cinque feature dello style detector e il conteggio dei colpi venivano letti
dalla UI direttamente dagli oggetti, mentre il thread audio li scriveva. Ora,
come tutto il resto, sono pubblicati dal thread audio in atomiche.

Anche la coda dei TAP: piu' tocchi di quanti ne contiene fra due callback
lasciavano il lettore puntato su celle gia' riusate.

### ONNX Runtime

Tre punti nel percorso del modello: due `OrtStatus` ignorati (una perdita per
frame, cinquanta volte al secondo, finche' dura il guasto), gli `OrtValue` di
uscita non liberati quando `Run` fallisce, e — la piu' seria — la lettura dei
logit che copiava `numClasses` float **anche se il tensore ne conteneva meno**,
cioe' leggeva oltre la fine del buffer di ONNX Runtime. Tutti e tre chiusi.

AddressSanitizer + UndefinedBehaviorSanitizer sulla suite: 91 test, zero
segnalazioni nel nostro codice. Le uniche perdite sono 64 byte una tantum dentro
`libonnxruntime` stessa.

### Il worker si svegliava mille volte al secondo

Non puo' uscire niente dalla catena finche' non e' arrivato un hop intero di
audio, che sono venti millisecondi. Il worker interrogava la FIFO ogni
millisecondo: **diciannove risvegli su venti** non trovavano niente da fare. Su
un dispositivo a batteria e' quello che costa, non i conti.

Ora aspetta all'incirca il tempo che manca al prossimo hop. Misurato,
alimentandolo a velocita' reale: **da 980 a 108 giri al secondo**. Non aggiunge
errore — ogni timestamp qui viene dall'indice di frame, e l'anticipo del thread
audio e' misurato, non assunto, quindi si limita a leggere un filo piu' grande.
Quando l'audio abbonda (come nella sonda, che gira venti volte piu' veloce del
tempo reale) non dorme affatto.

### Roba morta tolta

`TempoFollower` aveva tre membri scritti e mai letti, fra cui una compensazione
di latenza con tanto di setter chiamato dal tracker: due nomi per una correzione
sola, uno dei quali non faceva niente. L'anticipo vero e' quello misurato in
`songPhase`.

### Cosa **non** e' stato sistemato

`loadPercussionLoop` / `clearPercussionLoop` allocano e nessuno le chiama: il
percorso dei loop kit e' in architettura ma non ancora collegato. Chiamarle
mentre il callback gira rialloca i buffer che lo stretcher sta leggendo. Il
vincolo e' ora scritto nell'header. Se un giorno finiscono sotto un comando che
l'utente puo' toccare a brano in corso, serve un passaggio di consegne, non una
`assign` diretta.

## L'interfaccia (19 agosto, sera)

Due linee di lavoro unite: da `main` il **linguaggio visivo e il tema
light/dark** (palette fucsia, `AppLookAndFeel` piatto con la sottolineatura
sull'attivo, tipografia Futura/Avenir, bagliori radiali, interruttore del tema e
ascolto dell'impostazione di sistema); da qui il **layout** e i comandi che il
motore aveva ma non erano raggiungibili.

Prima era una colonna di righe tutte dello stesso peso, con in mezzo righe di
diagnostica (`AI ONNX | IPAD | nn 120 | p 0.42 valid`) che sono uscita di debug,
non una superficie da suonare; e in orizzontale non ci stava, quindi SWING ed
ENERGIA venivano semplicemente **nascoste**.

Ora sono due zone:

- **Palco** — quello che guardi mentre suoni: stato, BPM grande con **÷2** e
  **×2** ai lati, come il tempo è tenuto (`FISSO` / `VIVO` / `CERCO`, più
  «livello provvisorio» finché l'analisi non ha deciso), quattro pallini con
  l'uno segnato, e una riga sola sull'ingresso in italiano (`SENTO LA STANZA`)
  invece di quattro numeri.
- **Console** — quattro schede intitolate: **TRASPORTO** (START/STOP grandi più
  TAP), **PARTE**, **STRUMENTI**, **FEEL**. Un comando si cerca per *posto*, non
  per posizione in un elenco.

In orizzontale le due zone si affiancano invece di impilarsi: **non si nasconde
più niente**. I numeri di diagnostica sono tutti nel pannello **DBG**, che ora
riporta anche `tempo`, `livello`, `fold` e `ottava`.

### Il tema light era scuro

Era un difetto vero, non un'impressione. `ink()` — il colore con cui viene
riempito **ogni** bottone — valeva `0xff211d24` anche in chiaro, cioè un
quasi-nero, e il testo dei bottoni era forzato a bianco in entrambi i temi.
Risultato: fondo chiaro e sopra un muro di bottoni neri, che è esattamente come
appare un tema scuro. Ora `ink()` in chiaro è una superficie chiara e il testo
segue `text()`, tranne dove il riempimento è fucsia — lì il bianco è giusto.

Aggiustato anche quello che era tarato solo sul fondo nero: i bagliori radiali
(un terzo dell'intensità in chiaro, altrimenti sono una nuvola rosa sulla
pagina), l'ombra fucsia sotto le cifre del BPM, e il meter d'ingresso, che
sfumava verso `text()` e quindi in chiaro finiva nel nero.

E il font dei bottoni veniva scalato solo sull'altezza: in orizzontale, dove la
scheda PARTE ne mette cinque in fila, JUCE rispondeva con i puntini di
sospensione e si leggeva `MAR...` e `DAN...`. Ora la larghezza viene misurata e
il corpo ridotto quanto serve.

Guardata davvero in tutte e quattro le combinazioni — chiaro e scuro, verticale
e orizzontale — su un display virtuale, non solo compilata.

## Checklist device

Base — deve funzionare:

- [ ] Riga UI: AI ONNX + IPAD (non STUB, non MIXER)
- [ ] Tema: chiaro **davvero** chiaro, scuro scuro, e il cambio non lascia
      indietro nessun bottone. Ruota l'iPad in entrambi i temi: non deve sparire
      nessun comando e nessuna scritta deve finire con i puntini
- [ ] CLICK TEST → FOLLOWING, shaker dopo START
- [ ] Spotify SPEAKER → FOLLOWING senza TAP, shaker dopo START

Tempo e battuta — è qui che si vede il giro del 19 sera. Il pannello **DBG**
ora ha la riga `tempo FISSO/VIVO/CERCO · livello deciso/provvisorio · fold nnn`:
è la prima cosa da leggere quando qualcosa non torna.

- [ ] Su un brano registrato in studio il BPM deve **fermarsi** e restare fermo.
      Aspettati che si assesti in 5–10 s e che poi non si muova più di qualche
      decimo. Se oscilla di un BPM o più, segna la riga DBG
- [ ] La battuta deve arrivare al **quattro** prima di tornare all'uno. Guarda
      il pallino, non l'orecchio: è il difetto «uno, due, uno»
- [ ] Lo shaker deve cadere **dentro** il colpo del brano, non subito dopo. Se
      è ancora indietro, segna di quanto ti sembra e a che tempo
- [ ] Se parte sul movimento sbagliato, premi **SPOSTA L'1** finché non è a
      posto: deve restarci. **Atteso che sbagli**: automaticamente ci prende
      quattro volte su dieci
- [ ] Quando trova il tempo giusto non deve poi scappare via: se lo vedi salire
      o scendere in fretta dopo essersi assestato, segna `tempo` e `fold`
- [ ] Brano lento (sotto ~92) con ottavi pieni: **atteso che raddoppi**. Premi
      **÷2**: deve dimezzarsi e *restare* lì, non tornare su dopo qualche battuta
- [ ] USB-C + mic kit
- [ ] SPEAKER senza auto-inseguimento dello shaker
- [ ] Accelerando / rallentando
- [ ] Stacca/riattacca interfaccia
- [ ] Interruption / background

Le quattro parti — nuove, mai provate sul device:

- [ ] MARCHA su un brano latino
- [ ] ROCK su un brano con backbeat
- [ ] DANCE su qualcosa a quattro in piedi
- [ ] POP dove le altre tre sono troppo
- [ ] Cambio di parte mentre suona: non deve saltare né perdere la battuta
- [ ] SHAKER da solo (CONGAS off) e CONGAS da sole (SHAKER off)
- [ ] AUTO: che parte sceglie, e quanto spesso cambia idea. **Atteso: sbaglia.**
      Misurato a 3 casi su 9, cioè come tirare a indovinare — è per questo che è
      spento di default. Serve sapere *come* sbaglia, non se sbaglia

Suono e feel:

- [ ] SWING alto su uno shuffle: lo shaker deve andare con il brano, non contro
- [ ] ENERGIA da 0 a 1: deve cambiare peso, non solo volume
- [ ] Fill sull'ottava battuta: si sente? disturba?
- [ ] Colpi ripetuti veloci (1/16 a tempo alto): nessun click, nessun buco

Cosa segnare quando qualcosa non va: BPM mostrato, riga PARTE, stato, e il
pannello **DBG** in alto a destra, che ora riporta anche le cinque misure su cui
il rilevatore di stile decide.

## Cosa non è in questo MVP

- Particle filter completo di BeatNet (usiamo `BeatDecoder` + PLL)
- Signalsmith (loop TSM; shaker live a grain)
- Android / Oboe
- MIDI, Link, editor pattern
- Licenza JUCE commerciale (serve per App Store closed-source)

## File modello

| File | Ruolo |
|---|---|
| `Assets/Models/beatnet.onnx` | Pesi BeatNet (CMake embedda al configure) |
| `scripts/export_beatnet_onnx.py` | Scarica `model_1_weights.pt` e esporta ONNX |
| `scripts/train_export_beat_model.py` | Vecchio TCN-click, non è più il default |
| `Source/AI/LogSpectFeatures.*` | Feature allineate a BeatNet |
| `Source/AI/ModelLocator.cpp` | LSTM on, I/O `features`/`logits`/`h0`/`c0` |

## Comandi

```bash
./scripts/run-tests.sh                 # host
./scripts/configure-ios.sh             # re-embed + Xcode proj
./scripts/build-simulator.sh
```

Diagnostica del tracking (host, non fanno parte della suite — sono lenti):

```bash
cmake --build build-host --target VPProbe        # 30 brani a tempo fisso, motore intero
./build-host/VPProbe_artefacts/Release/VPProbe --ipad     # cassa -> stanza -> microfono
./build-host/VPProbe_artefacts/Release/VPProbe --mixer    # mandata di linea
./build-host/VPProbe_artefacts/Release/VPProbe --ipad --live   # band vera: deriva + scarto umano
./build-host/VPProbe_artefacts/Release/VPReplay --anchor act_*.txt   # decoder da solo, con l'ancora di livello
./build-host/VPProbe_artefacts/Release/VPProbe --trace --mixer straight 104   # un caso solo

cmake --build build-host --target VPActivations VPReplay
./build-host/VPActivations_artefacts/Release/VPActivations 104 straight > act.txt
./build-host/VPReplay_artefacts/Release/VPReplay act.txt              # solo il decoder
./build-host/VPReplay_artefacts/Release/VPReplay --levels act.txt     # la lite sulle ottave

cmake --build build-host --target VPTiming VPCpu
./build-host/VPTiming_artefacts/Release/VPTiming --ipad    # dove cade il colpo, per modalita'
./build-host/VPTiming_artefacts/Release/VPTiming --mixer
./build-host/VPCpu_artefacts/Release/VPCpu 256     # callback contro il suo budget, e CPU totale
```

Sanitizer (lenti, ma trovano quello che rileggere il codice non trova):

```bash
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DVP_BUILD_TESTS=ON \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-asan --target VPTests && ./build-asan/VPTests_artefacts/RelWithDebInfo/VPTests

cmake -S . -B build-tsan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DVP_BUILD_TESTS=ON \
  -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"
cmake --build build-tsan --target VPTests && ./build-tsan/VPTests_artefacts/RelWithDebInfo/VPTests
```

Sotto TSan il worker gira un ordine di grandezza piu' lento del tempo reale e la
FIFO va in overrun di continuo. Due test dicono INCONCLUSIVE invece di fallire,
perche' la loro premessa non regge in quelle condizioni e non e' quella che
stanno misurando: `tap-downbeat` (serve un orologio fermo da cui scegliere il
movimento su cui battere) e `bar-integrity` (serve un'analisi che non stia
perdendo audio — quel caso e' di `bar-starved`, che lo cerca apposta).

`VPProbe` vuole ONNX Runtime host (`./scripts/fetch_onnxruntime.sh`): senza, gira
lo stub e non misura niente di utile.
