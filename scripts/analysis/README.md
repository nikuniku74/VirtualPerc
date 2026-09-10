# Banco su materiale reale

Strumenti per misurare l'app contro registrazioni vere invece che sintetiche.
Nati il 10/09/2026 con la prima registrazione live disponibile (mandata del
banco, 97 minuti). Vedi il RECAP in `docs/HANDOFF_TEMPO.md` e gli item 31-37 di
`docs/TODO.md`.

Tutti prendono un wav; quelli che leggono la fase prendono anche il file scritto
da `VPTrack --pulses`.

| file | cosa misura |
|---|---|
| `profile.py wav minuti` | livello, quota di banda bassa e tempo grezzo ogni 10 s. Per trovare i confini fra i brani in una registrazione lunga. |
| `tempocurve.py wav lo hi` | tempogramma indipendente a finestra di 12 s: un **secondo parere** sul tempo vero. Usare solo i punti con nitidezza > 0.15. **Non è verità di fase.** |
| `hist.py wav pulses [W]` | l'energia degli attacchi piegata sulla fase dell'orologio, istogramma a 24 bin. «struttura» = picco/media: sopra 2 la griglia è sulla musica. Dove sta il picco e di quanto si sposta fra finestre. |
| `prec.py wav pulses lo hi` | i tre indicatori di precisione insieme: struttura, spostamento di fase fra finestre, deviazione degli strattoni della griglia. |
| `score.py trace` | il BPM pubblicato contro una curva di riferimento: errore medio, uscite oltre il 4% e loro durata. |
| `extract_live.swift input.m4a dir` | crea i cinque estratti centrali, con 3 s di silenzio iniziale, usati da `bench_live.py` sulla serata Flamingo. |

## Due trappole, misurate

**La risultante della prima armonica non funziona su materiale swingato.**
`fold.py` e `phase2.py` sono i due tentativi falliti, tenuti apposta: su
terzine l'energia cade a 0, 1/3 e 2/3 del battito e la risultante si annulla
**anche con l'aggancio perfetto**. Davano «FUORI» su tutto un brano che era
agganciato. Usare `hist.py`.

**Il BPM pubblicato non basta e inganna.** Sul brano 1 del live sta fra 80.5 e
84 con confidenza 1.00 mentre la velocità istantanea della griglia scende a 66.
Quello che si sente è la griglia, e si ricava dalla derivata della fase in
`--pulses`, non da `s.bpm` né da `s.clockBpm` (che sono lo stesso numero).
