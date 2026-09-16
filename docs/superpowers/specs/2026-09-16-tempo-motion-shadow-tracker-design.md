# Tracker ombra per il moto continuo del tempo

## Obiettivo

VirtualPerc deve restare agganciato causalmente al BPM e alla fase di un brano
caricato o di una mandata mixer/live anche durante accelerando e rallentando.
Quando il sistema comincia a uscire deve correggere appena esiste evidenza
causale sufficiente, senza inventare accelerazioni o rallentamenti su un tempo
stabile.

Il percorso diretto viene completato per primo. Il microfono iPad resta fuori da
questa fase finche' file e mixer non superano sia i banchi sintetici sia la
verifica su audio reale.

## Vincoli non negoziabili

- Il clock musicale non riparte perche' il BPM cambia.
- Nessun beat, impulso o fase puo' arretrare, duplicarsi o essere saltato.
- Nessuno snap di fase mentre la parte suona.
- Una singola misura non puo' scegliere BPM o posizione.
- Gradini confermati, ottave, TAP e tempo manuale conservano la priorita'.
- Il nuovo stato vive sul worker; audio thread senza allocazioni, lock, I/O o
  lavoro non limitato.
- La logica deve essere globale e non contenere soglie derivate da un brano
  specifico.

## Problema misurato

`BeatDecoder` divide oggi il mondo in `fixed` e `live`. In `fixed` il BPM
pubblicato segue un'ancora molto stabile; il fit corto puo' vedere una rampa
diversi beat prima che una delle condizioni binarie autorizzi `live`. Sulla
rampa 100->110 BPM in 12 secondi la verita' ha gia' raggiunto 104,17 BPM mentre
il decoder pubblica 100,07 e il clock e' 69,9 ms in ritardo.

Un rilascio binario guidato dalla curvatura locale non e' abbastanza sicuro. Il
banco globale ha trovato otto prove false su 128 tempi fissi con il selettore
abbastanza rapido da aiutare tutte le rampe. Tre varianti di soglia non hanno
separato rapidita' e falsi positivi. La curvatura resta quindi diagnostica.

Il clock possiede gia' un trim derivato dalla pendenza della fase. Aumentarlo non
risolve il problema: la stessa pendenza puo' provenire da onset in ritardo o da
un passaggio senza batteria. La decisione sul moto deve restare nel decoder.

## Decisione

Introdurre `TempoMotionTracker`, un estimatore causale parallelo che non possiede
direttamente il tempo. Il tracker mantiene uno stato dinamico ombra e produce:

- periodo locale previsto;
- variazione del periodo per beat;
- incertezza normalizzata;
- autorita' di moto tra zero e uno;
- ragione diagnostica di attivazione o rifiuto.

`fixed` e `live` restano responsabili del comportamento di lungo periodo. Il
tracker ombra copre soltanto il ritardo iniziale: quando il moto e' provato,
fornisce a `fixed` un target temporaneo e limitato; quando il normale criterio
entra in `live`, il target esistente prende il controllo senza discontinuita'.

## Componente `TempoMotionTracker`

Il componente usa soltanto memoria a dimensione fissa e operazioni limitate.
Riceve una osservazione per ogni beat gia' accettato dal decoder:

- timestamp del beat;
- numero di intervalli di griglia dall'osservazione precedente;
- forza del beat;
- periodo attualmente commesso;
- residuo e copertura dei fit corto e lungo;
- stato della transizione brusca.

Un intervallo che salta un beat viene diviso per il numero intero di quarti
attraversati. Intervalli incompatibili con la griglia, suddivisioni e picchi non
accettati non entrano nel tracker.

Lo stato dinamico e' un filtro robusto periodo/velocita'. Prima di ogni
osservazione predice il periodo successivo; l'innovazione e' la differenza tra
periodo osservato e previsto. Una finestra fissa conserva la scala robusta delle
innovazioni. La velocita' puo' crescere soltanto quando:

1. le innovazioni superano il rumore stimato del materiale;
2. mantengono la stessa direzione;
3. fit corto, tracker e periodo commesso concordano sul segno;
4. copertura e residui indicano beat utilizzabili;
5. non esiste una transizione brusca o un argomento d'ottava.

L'incertezza cresce quando manca evidenza o le innovazioni si contraddicono.
L'autorita' resta esattamente zero finche' il modello dinamico non e' separato
dal modello a periodo costante. Dopo la prova cresce gradualmente, al massimo
una volta per beat; una contraddizione la azzera senza cambiare regime.

## Target temporaneo in `fixed`

Con autorita' zero il ramo `fixed` deve essere bit-identico al controllo.

Con autorita' positiva, `fixed` calcola un target ponte tra il BPM commesso e il
BPM locale previsto. Il target:

- non modifica `fixedAnchorBpm`;
- non ricostruisce la beat history;
- non genera `transitionSerial`;
- non muove `gridAnchorSec`;
- non puo' cambiare piu' dello 0,75% per beat;
- non puo' allontanarsi oltre il 4% dal BPM commesso;
- passa attraverso il normale `commit`, quindi il clock continua a correggere
  soltanto tramite velocita'.

I limiti sono barriere di sicurezza iniziali, non numeri da ottimizzare su una
singola rampa. Se il normale decoder entra in `live`, il ponte si spegne e il
fit live esistente continua dallo stesso target.

## Priorita' e reset

Il tracker non ha autorita' quando uno dei percorsi seguenti possiede il tempo:

- TAP o tempo manuale;
- transizione brusca `suspected` o `rapid`;
- cambio d'ottava o ricostruzione della griglia;
- epoch di nuovo input;
- discontinuita' del FIFO;
- evidenza scaduta per assenza di beat.

Un dropout cancella velocita', incertezza acquisita e autorita', ma conserva il
BPM commesso come fa il decoder attuale. Un nuovo input azzera anche il modello
costante. Valori non finiti o periodi fuori range causano un reset locale e non
una pubblicazione.

## Diagnostica

La snapshot espone stato ombra, BPM previsto, velocita', incertezza e autorita'.
Questi campi spiegano perche' una correzione e' iniziata o e' stata negata; non
sono letti dal clock. `fitPeriodCurve` resta disponibile per confrontare il
nuovo modello con il vecchio diagnostico durante lo sviluppo.

## Strategia di test

### Test unitari prima dell'implementazione

Il tracker puro riceve intervalli sintetici e deve provare:

- autorita' sempre zero su periodo fisso con jitter, outlier, beat mancanti e
  falsi picchi;
- autorita' crescente su accelerando e rallentando;
- reset su inversione, dropout, transizione e cambio di griglia;
- normalizzazione corretta degli intervalli che attraversano beat mancanti;
- stato finito e limitato a 52, 100, 168 BPM.

### Banco globale A/B

`probe_motion_matrix` esegue controllo e candidato sugli stessi eventi. Durante
lo sviluppo un seam temporaneo consente l'A/B, ma non resta nel prodotto.

- Tempo fisso e tutta la popolazione a gradino: sequenze BPM e clock
  bit-identiche.
- Moto continuo con livello metrico corretto: media e p95 migliorano in ogni
  offset, non soltanto nella media aggregata.
- Uscita oltre 50 ms: rientro entro due beat dopo la prova causale.
- Casi d'ottava ambigui: gate di non-regressione separato, mai usati per
  attribuire al tracker la qualita' del moto.

### Gate mirati

Le quattro rampe `VPAlign --ramps` devono passare. I controlli fissi, il buco
batteria e tutti i gradini gia' protetti non possono peggiorare. Il gate
bit-identico riguarda l'intera famiglia sintetica a gradino; i test mirati
restano obbligatori almeno:

- `VPTests --tempo-step`;
- `VPTests --tempo-slow`;
- `VPTests --evidence`;
- `VPTests --new-input`;
- `VPTests --bar`;
- `probe_tempo_step`;
- `probe_recovery`.

### Audio reale

La chiusura del percorso diretto richiede almeno due estratti con beat-grid
affidabile: un accelerando e un rallentando. Il render della parte viene
confrontato con la griglia e ascoltato. Il riferimento tempogramma di Flamingo
resta un segnale debole e non puo' sostituire questo gate.

## Criterio di completamento

“Immediato” significa che la correzione inizia non oltre il beat successivo alla
chiusura della prova causale. Non significa reagire prima che il segnale possa
distinguere una rampa dal jitter.

L'obiettivo e' raggiunto soltanto quando:

1. fisso e gradini restano invariati nei banchi deterministici;
2. ogni popolazione continua migliora media e p95;
3. tutte le rampe mirate e i gate tempo passano;
4. un errore oltre 50 ms rientra entro due beat dopo la prova;
5. accelerando e rallentando reali con beat-grid affidabile superano misura e
   ascolto;
6. le invarianti realtime e di continuita' del clock restano provate.

## Emendamento: prova ibrida al confine `fixed -> live`

Il primo tentativo di Task 5 ha falsificato l'ipotesi che bastasse accordare le
soglie del regressore lineare. Con le soglie originali nessuna delle quattro
popolazioni continue rapide ha prodotto autorita'. Ridurre la significativita'
da 4 a 3 ha attivato soltanto una popolazione continua, ma ha anche dato
autorita' a due popolazioni a gradino, cambiandone i trace hash e producendo
violazioni del rientro. Ridurre il numero di prove ha mostrato lo stesso
conflitto. Nessuna di queste varianti viene mantenuta.

La causa misurata e' l'ordine delle decisioni. Il tracker osserva il beat prima
che il decoder valuti il regime; l'ingresso in `fixed` azzera correttamente il
modello e il beat successivo serve da ancora. Sul moto continuo, pero', il
rilascio `fixed -> live` esistente usa l'evidenza utile prima che il tracker
possa accumulare tre prove. Sul gradino il decoder puo' restare `fixed` piu' a
lungo, mentre la finestra che attraversa i due plateau assomiglia
temporaneamente a una retta. Una soglia piu' permissiva premia quindi il caso
sbagliato.

Il confronto tra segno della pendenza e differenza tra fit corto e BPM commesso
non e' la causa operativa: nei campioni con finestra piena e significativita'
alta i segni concordano. Le discordie osservate appartengono soprattutto a
finestre ancora incomplete dopo un reset.

### Decisione ibrida

La prova viene divisa in due livelli indipendenti:

1. **ponte minimo sul beat di rilascio:** il tracker espone il fronte della
   prima prova stretta. Se, sullo stesso beat accettato, quel fronte coincide
   con il predicato di rilascio `fixed -> live` gia' esistente, il decoder puo'
   applicare una sola correzione preventiva con autorita' massima `0.35`.
   La correzione avviene mentre il regime e' ancora `fixed`, prima di entrare
   in `live`; poi il percorso `live` esistente prende il controllo;
2. **autorita' piena dalla forma:** se il decoder resta `fixed`, una finestra
   causale da sette a dodici punti confronta quattro spiegazioni dei residui di
   fase: affine, quadratica, hinge a due rette e affine con un outlier escluso.
   Il quadratico deve vincere su due beat accettati consecutivi prima che
   l'autorita' possa crescere oltre il ponte minimo. Una vittoria hinge
   identifica un gradino, azzera la prova e mette la finestra in quarantena.

Il ponte minimo e' deliberatamente falsificabile: nei banchi rapidi analizzati
la coincidenza prima-prova/rilascio appare sul moto continuo e non sui gradini,
ma non viene trattata come un'invariante finche' banco pieno, offset
indipendenti e gate mirati non lo confermano. Se cambia anche un solo trace
hash a gradino, il ponte minimo viene rifiutato; non se ne allarga la soglia.

### Modello dei residui

All'ingresso in `fixed` il tracker salva soltanto un nuovo riferimento
`entryTime`, il periodo commesso `p0` e un indice di griglia cumulativo. Non
porta velocita', autorita' o pendenza dal regime precedente. Per ogni beat
accettato:

```text
gridIndex += gridQuarterSteps
residual = beatTime - entryTime - gridIndex * p0
```

Una variazione continua del periodo curva il residuo cumulativo; un gradino
crea due tratti quasi lineari uniti da un hinge; un tempo costante o uno
spostamento permanente di fase resta affine; un onset isolato viene assorbito
dal modello con un punto escluso. Tutti i fit usano array fissi e provano un
numero limitato di split, senza allocazioni.

Il verdetto usa miglioramenti normalizzati rispetto al rumore robusto della
finestra, non soglie in BPM derivate da un brano. Dai sette punti in poi il
modello viene rivalutato a ogni beat fino a dodici punti; non esiste una prova
forzata allo scadere. Un moto troppo debole o ambiguo resta senza autorita'.

### Stato e priorita'

Il tracker aggiunge diagnostica limitata per:

- fronte della prima prova stretta;
- modello vincente (`affine`, `quadratico`, `hinge`, `outlier`);
- margine quadratico-versus-hinge;
- beat consecutivi di conferma;
- beat residui di quarantena.

Transizione `suspected`/`rapid`, refit, ottava, ricostruzione della griglia,
input epoch, discontinuita', percorso non diretto, perdita di `fixed`,
osservazione non valida o evidenza scaduta azzerano immediatamente ogni
autorita'. Un hinge o un edge brusco osservato dal decoder apre una quarantena
di dodici beat. Durante la quarantena il tracker puo' aggiornare la diagnostica,
ma non pubblicare autorita'. Un gradino completato diventa poi affine e non puo'
essere reinterpretato come moto.

Il ramo con autorita' zero resta bit-identico. Entrambi i livelli usano solo
`commit`, il limite di `0.75%` per beat e la distanza massima del `4%`; non
scrivono `fixedAnchorBpm`, beat history, `gridAnchorSec`, stato o seriali di
transizione, ottava o battuta.

### Verifica dell'emendamento

Prima dell'implementazione devono fallire test mirati che provano:

- la coincidenza prima-prova/rilascio su una rampa e la sua assenza su un
  gradino con finestra apparentemente lineare;
- la singola correzione minima prima del passaggio a `live`;
- la vittoria quadratica su accelerando e rallentando;
- la vittoria hinge su gradini tra ogni coppia di posizioni della finestra,
  anche col primo beat nuovo mancante;
- affine su tempo fisso e spostamento di fase permanente;
- outlier su un beat spostato e corretta normalizzazione dei beat mancanti;
- quarantena, reset, rail `0.75%`/`4%` e seriali/griglia immutati.

L'accettazione resta quella originale e non viene negoziata: trace hash e
autorita' dei gradini invariati, miglioramento di media e p95 in ogni
popolazione continua, autorita' continua non nulla e zero violazioni oltre due
beat. Prima si eseguono quattro offset rapidi; poi banco pieno, nuovi offset,
`VPAlign`, gate tempo e probe standalone. Soltanto dopo questi risultati si
procede all'audio reale.
