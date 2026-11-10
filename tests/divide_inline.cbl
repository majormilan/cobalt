       IDENTIFICATION DIVISION.
       PROGRAM-ID. DIVIDE-INLINE.
      * Division, rounding and products cut back to their scale,
      * computed inline: every sign, constant and field divisors,
      * division by zero, overflow, all usages.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       77  DA  PIC S9(7)V99.
       77  DB  PIC S9(5)V9.
       77  DI  PIC S9(9).
       77  DJ  PIC S9(4).
       77  DR  PIC S9(9)V99.
       77  DS  PIC S9(11).
       77  DT  PIC S9(3)V9.
       77  BA  PIC S9(7)V99 COMP.
       77  BB  PIC S9(5)V9 COMP.
       77  BI  PIC S9(9) COMP.
       77  BJ  PIC S9(4) COMP.
       77  BR  PIC S9(9)V99 COMP.
       77  BS  PIC S9(11) COMP.
       77  BT  PIC S9(3)V9 COMP.
       77  PA  PIC S9(7)V99 COMP-3.
       77  PB  PIC S9(5)V9 COMP-3.
       77  PI  PIC S9(9) COMP-3.
       77  PJ  PIC S9(4) COMP-3.
       77  PR  PIC S9(9)V99 COMP-3.
       77  PS  PIC S9(11) COMP-3.
       77  PT  PIC S9(3)V9 COMP-3.
       77  NA  PIC S9(7)V99 COMP-5.
       77  NB  PIC S9(5)V9 COMP-5.
       77  NI  PIC S9(9) COMP-5.
       77  NJ  PIC S9(4) COMP-5.
       77  NR  PIC S9(9)V99 COMP-5.
       77  NS  PIC S9(11) COMP-5.
       77  NT  PIC S9(3)V9 COMP-5.
       77  BIG PIC S9(17) COMP-5.
       77  RES PIC S9(18) COMP-5.
       77  W17 PIC S9(17) COMP-3.
       77  W5  PIC S9(17) COMP-5.
       77  WV  PIC S9(15)V99 COMP-3.
       77  WD  PIC S9(9) COMP-5.
       77  WQ  PIC S9(16)V99 COMP-3.
       77  M1  PIC S9(18) COMP-5.
       77  M2  PIC S9(18) COMP-5.
       77  M3  PIC S9(18) COMP-5.
       77  MV  PIC S9(16)V99 COMP-5.
       PROCEDURE DIVISION.
           MOVE 0.01 TO DA. MOVE 88489.2 TO DB.
           MOVE 588920085 TO DI. MOVE 0 TO DJ.
           DIVIDE DB INTO DA GIVING DR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY DR.
           DIVIDE DA BY 3 GIVING DR ROUNDED.
           DISPLAY DR.
           DIVIDE DA BY -7 GIVING DR.
           DISPLAY DR.
           DIVIDE DJ INTO DI GIVING DS REMAINDER DJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY DS ' ' DJ.
           COMPUTE DR ROUNDED = (DA * DB + DI) / 3.
           DISPLAY DR.
           COMPUTE DT ROUNDED = DA * DB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY DT.
           MULTIPLY DA BY DB GIVING DR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY DR.
           COMPUTE DS ROUNDED = DI / DB * DA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY DS.
           MOVE 0.5 TO DA. MOVE 80179.3 TO DB.
           MOVE -756892036 TO DI. MOVE -9999 TO DJ.
           DIVIDE DB INTO DA GIVING DR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY DR.
           DIVIDE DA BY 3 GIVING DR ROUNDED.
           DISPLAY DR.
           DIVIDE DA BY -7 GIVING DR.
           DISPLAY DR.
           DIVIDE DJ INTO DI GIVING DS REMAINDER DJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY DS ' ' DJ.
           COMPUTE DR ROUNDED = (DA * DB + DI) / 3.
           DISPLAY DR.
           COMPUTE DT ROUNDED = DA * DB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY DT.
           MULTIPLY DA BY DB GIVING DR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY DR.
           COMPUTE DS ROUNDED = DI / DB * DA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY DS.
           MOVE -0.01 TO DA. MOVE 14788.1 TO DB.
           MOVE -971837491 TO DI. MOVE 9999 TO DJ.
           DIVIDE DB INTO DA GIVING DR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY DR.
           DIVIDE DA BY 3 GIVING DR ROUNDED.
           DISPLAY DR.
           DIVIDE DA BY -7 GIVING DR.
           DISPLAY DR.
           DIVIDE DJ INTO DI GIVING DS REMAINDER DJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY DS ' ' DJ.
           COMPUTE DR ROUNDED = (DA * DB + DI) / 3.
           DISPLAY DR.
           COMPUTE DT ROUNDED = DA * DB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY DT.
           MULTIPLY DA BY DB GIVING DR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY DR.
           COMPUTE DS ROUNDED = DI / DB * DA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY DS.
           MOVE 8326906.6 TO DA. MOVE -68078.5 TO DB.
           MOVE 711860140 TO DI. MOVE -1 TO DJ.
           DIVIDE DB INTO DA GIVING DR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY DR.
           DIVIDE DA BY 3 GIVING DR ROUNDED.
           DISPLAY DR.
           DIVIDE DA BY -7 GIVING DR.
           DISPLAY DR.
           DIVIDE DJ INTO DI GIVING DS REMAINDER DJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY DS ' ' DJ.
           COMPUTE DR ROUNDED = (DA * DB + DI) / 3.
           DISPLAY DR.
           COMPUTE DT ROUNDED = DA * DB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY DT.
           MULTIPLY DA BY DB GIVING DR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY DR.
           COMPUTE DS ROUNDED = DI / DB * DA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY DS.
           MOVE 2.5 TO DA. MOVE 0 TO DB.
           MOVE 871327446 TO DI. MOVE 3 TO DJ.
           DIVIDE DB INTO DA GIVING DR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY DR.
           DIVIDE DA BY 3 GIVING DR ROUNDED.
           DISPLAY DR.
           DIVIDE DA BY -7 GIVING DR.
           DISPLAY DR.
           DIVIDE DJ INTO DI GIVING DS REMAINDER DJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY DS ' ' DJ.
           COMPUTE DR ROUNDED = (DA * DB + DI) / 3.
           DISPLAY DR.
           COMPUTE DT ROUNDED = DA * DB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY DT.
           MULTIPLY DA BY DB GIVING DR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY DR.
           COMPUTE DS ROUNDED = DI / DB * DA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY DS.
           MOVE -1 TO DA. MOVE -42138.5 TO DB.
           MOVE -572888427 TO DI. MOVE 3 TO DJ.
           DIVIDE DB INTO DA GIVING DR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY DR.
           DIVIDE DA BY 3 GIVING DR ROUNDED.
           DISPLAY DR.
           DIVIDE DA BY -7 GIVING DR.
           DISPLAY DR.
           DIVIDE DJ INTO DI GIVING DS REMAINDER DJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY DS ' ' DJ.
           COMPUTE DR ROUNDED = (DA * DB + DI) / 3.
           DISPLAY DR.
           COMPUTE DT ROUNDED = DA * DB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY DT.
           MULTIPLY DA BY DB GIVING DR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY DR.
           COMPUTE DS ROUNDED = DI / DB * DA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY DS.
           MOVE 9371134.55 TO DA. MOVE -123.45 TO DB.
           MOVE -358355719 TO DI. MOVE 7 TO DJ.
           DIVIDE DB INTO DA GIVING DR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY DR.
           DIVIDE DA BY 3 GIVING DR ROUNDED.
           DISPLAY DR.
           DIVIDE DA BY -7 GIVING DR.
           DISPLAY DR.
           DIVIDE DJ INTO DI GIVING DS REMAINDER DJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY DS ' ' DJ.
           COMPUTE DR ROUNDED = (DA * DB + DI) / 3.
           DISPLAY DR.
           COMPUTE DT ROUNDED = DA * DB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY DT.
           MULTIPLY DA BY DB GIVING DR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY DR.
           COMPUTE DS ROUNDED = DI / DB * DA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY DS.
           MOVE 8715852.25 TO DA. MOVE 123.45 TO DB.
           MOVE -352836262 TO DI. MOVE 0 TO DJ.
           DIVIDE DB INTO DA GIVING DR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY DR.
           DIVIDE DA BY 3 GIVING DR ROUNDED.
           DISPLAY DR.
           DIVIDE DA BY -7 GIVING DR.
           DISPLAY DR.
           DIVIDE DJ INTO DI GIVING DS REMAINDER DJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY DS ' ' DJ.
           COMPUTE DR ROUNDED = (DA * DB + DI) / 3.
           DISPLAY DR.
           COMPUTE DT ROUNDED = DA * DB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY DT.
           MULTIPLY DA BY DB GIVING DR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY DR.
           COMPUTE DS ROUNDED = DI / DB * DA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY DS.
           MOVE 0.01 TO DA. MOVE 123.45 TO DB.
           MOVE 757754190 TO DI. MOVE -9999 TO DJ.
           DIVIDE DB INTO DA GIVING DR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY DR.
           DIVIDE DA BY 3 GIVING DR ROUNDED.
           DISPLAY DR.
           DIVIDE DA BY -7 GIVING DR.
           DISPLAY DR.
           DIVIDE DJ INTO DI GIVING DS REMAINDER DJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY DS ' ' DJ.
           COMPUTE DR ROUNDED = (DA * DB + DI) / 3.
           DISPLAY DR.
           COMPUTE DT ROUNDED = DA * DB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY DT.
           MULTIPLY DA BY DB GIVING DR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY DR.
           COMPUTE DS ROUNDED = DI / DB * DA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY DS.
           MOVE -1 TO DA. MOVE -1 TO DB.
           MOVE -877591430 TO DI. MOVE 0 TO DJ.
           DIVIDE DB INTO DA GIVING DR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY DR.
           DIVIDE DA BY 3 GIVING DR ROUNDED.
           DISPLAY DR.
           DIVIDE DA BY -7 GIVING DR.
           DISPLAY DR.
           DIVIDE DJ INTO DI GIVING DS REMAINDER DJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY DS ' ' DJ.
           COMPUTE DR ROUNDED = (DA * DB + DI) / 3.
           DISPLAY DR.
           COMPUTE DT ROUNDED = DA * DB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY DT.
           MULTIPLY DA BY DB GIVING DR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY DR.
           COMPUTE DS ROUNDED = DI / DB * DA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY DS.
           MOVE 0.01 TO DA. MOVE -96385.5 TO DB.
           MOVE 691655124 TO DI. MOVE 7 TO DJ.
           DIVIDE DB INTO DA GIVING DR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY DR.
           DIVIDE DA BY 3 GIVING DR ROUNDED.
           DISPLAY DR.
           DIVIDE DA BY -7 GIVING DR.
           DISPLAY DR.
           DIVIDE DJ INTO DI GIVING DS REMAINDER DJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY DS ' ' DJ.
           COMPUTE DR ROUNDED = (DA * DB + DI) / 3.
           DISPLAY DR.
           COMPUTE DT ROUNDED = DA * DB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY DT.
           MULTIPLY DA BY DB GIVING DR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY DR.
           COMPUTE DS ROUNDED = DI / DB * DA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY DS.
           MOVE 6824681.23 TO DA. MOVE -1 TO DB.
           MOVE 339413524 TO DI. MOVE 1 TO DJ.
           DIVIDE DB INTO DA GIVING DR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY DR.
           DIVIDE DA BY 3 GIVING DR ROUNDED.
           DISPLAY DR.
           DIVIDE DA BY -7 GIVING DR.
           DISPLAY DR.
           DIVIDE DJ INTO DI GIVING DS REMAINDER DJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY DS ' ' DJ.
           COMPUTE DR ROUNDED = (DA * DB + DI) / 3.
           DISPLAY DR.
           COMPUTE DT ROUNDED = DA * DB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY DT.
           MULTIPLY DA BY DB GIVING DR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY DR.
           COMPUTE DS ROUNDED = DI / DB * DA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY DS.
           MOVE 2.5 TO DA. MOVE 123.45 TO DB.
           MOVE 917082469 TO DI. MOVE -3 TO DJ.
           DIVIDE DB INTO DA GIVING DR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY DR.
           DIVIDE DA BY 3 GIVING DR ROUNDED.
           DISPLAY DR.
           DIVIDE DA BY -7 GIVING DR.
           DISPLAY DR.
           DIVIDE DJ INTO DI GIVING DS REMAINDER DJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY DS ' ' DJ.
           COMPUTE DR ROUNDED = (DA * DB + DI) / 3.
           DISPLAY DR.
           COMPUTE DT ROUNDED = DA * DB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY DT.
           MULTIPLY DA BY DB GIVING DR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY DR.
           COMPUTE DS ROUNDED = DI / DB * DA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY DS.
           MOVE 1 TO DA. MOVE 56196.9 TO DB.
           MOVE -211235238 TO DI. MOVE 0 TO DJ.
           DIVIDE DB INTO DA GIVING DR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY DR.
           DIVIDE DA BY 3 GIVING DR ROUNDED.
           DISPLAY DR.
           DIVIDE DA BY -7 GIVING DR.
           DISPLAY DR.
           DIVIDE DJ INTO DI GIVING DS REMAINDER DJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY DS ' ' DJ.
           COMPUTE DR ROUNDED = (DA * DB + DI) / 3.
           DISPLAY DR.
           COMPUTE DT ROUNDED = DA * DB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY DT.
           MULTIPLY DA BY DB GIVING DR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY DR.
           COMPUTE DS ROUNDED = DI / DB * DA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY DS.
           MOVE 9566828.86 TO BA. MOVE -0.05 TO BB.
           MOVE 311742160 TO BI. MOVE -1 TO BJ.
           DIVIDE BB INTO BA GIVING BR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY BR.
           DIVIDE BA BY 3 GIVING BR ROUNDED.
           DISPLAY BR.
           DIVIDE BA BY -7 GIVING BR.
           DISPLAY BR.
           DIVIDE BJ INTO BI GIVING BS REMAINDER BJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY BS ' ' BJ.
           COMPUTE BR ROUNDED = (BA * BB + BI) / 3.
           DISPLAY BR.
           COMPUTE BT ROUNDED = BA * BB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY BT.
           MULTIPLY BA BY BB GIVING BR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY BR.
           COMPUTE BS ROUNDED = BI / BB * BA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY BS.
           MOVE 8483951.54 TO BA. MOVE -37509.6 TO BB.
           MOVE -319677209 TO BI. MOVE -1 TO BJ.
           DIVIDE BB INTO BA GIVING BR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY BR.
           DIVIDE BA BY 3 GIVING BR ROUNDED.
           DISPLAY BR.
           DIVIDE BA BY -7 GIVING BR.
           DISPLAY BR.
           DIVIDE BJ INTO BI GIVING BS REMAINDER BJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY BS ' ' BJ.
           COMPUTE BR ROUNDED = (BA * BB + BI) / 3.
           DISPLAY BR.
           COMPUTE BT ROUNDED = BA * BB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY BT.
           MULTIPLY BA BY BB GIVING BR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY BR.
           COMPUTE BS ROUNDED = BI / BB * BA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY BS.
           MOVE -1 TO BA. MOVE -38138.9 TO BB.
           MOVE 38939217 TO BI. MOVE 0 TO BJ.
           DIVIDE BB INTO BA GIVING BR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY BR.
           DIVIDE BA BY 3 GIVING BR ROUNDED.
           DISPLAY BR.
           DIVIDE BA BY -7 GIVING BR.
           DISPLAY BR.
           DIVIDE BJ INTO BI GIVING BS REMAINDER BJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY BS ' ' BJ.
           COMPUTE BR ROUNDED = (BA * BB + BI) / 3.
           DISPLAY BR.
           COMPUTE BT ROUNDED = BA * BB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY BT.
           MULTIPLY BA BY BB GIVING BR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY BR.
           COMPUTE BS ROUNDED = BI / BB * BA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY BS.
           MOVE 2012984.03 TO BA. MOVE -93633.8 TO BB.
           MOVE 590337250 TO BI. MOVE 7 TO BJ.
           DIVIDE BB INTO BA GIVING BR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY BR.
           DIVIDE BA BY 3 GIVING BR ROUNDED.
           DISPLAY BR.
           DIVIDE BA BY -7 GIVING BR.
           DISPLAY BR.
           DIVIDE BJ INTO BI GIVING BS REMAINDER BJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY BS ' ' BJ.
           COMPUTE BR ROUNDED = (BA * BB + BI) / 3.
           DISPLAY BR.
           COMPUTE BT ROUNDED = BA * BB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY BT.
           MULTIPLY BA BY BB GIVING BR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY BR.
           COMPUTE BS ROUNDED = BI / BB * BA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY BS.
           MOVE 2.5 TO BA. MOVE -70873.5 TO BB.
           MOVE 967243942 TO BI. MOVE -9999 TO BJ.
           DIVIDE BB INTO BA GIVING BR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY BR.
           DIVIDE BA BY 3 GIVING BR ROUNDED.
           DISPLAY BR.
           DIVIDE BA BY -7 GIVING BR.
           DISPLAY BR.
           DIVIDE BJ INTO BI GIVING BS REMAINDER BJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY BS ' ' BJ.
           COMPUTE BR ROUNDED = (BA * BB + BI) / 3.
           DISPLAY BR.
           COMPUTE BT ROUNDED = BA * BB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY BT.
           MULTIPLY BA BY BB GIVING BR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY BR.
           COMPUTE BS ROUNDED = BI / BB * BA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY BS.
           MOVE -5858801.59 TO BA. MOVE 12809.4 TO BB.
           MOVE -714863586 TO BI. MOVE 1 TO BJ.
           DIVIDE BB INTO BA GIVING BR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY BR.
           DIVIDE BA BY 3 GIVING BR ROUNDED.
           DISPLAY BR.
           DIVIDE BA BY -7 GIVING BR.
           DISPLAY BR.
           DIVIDE BJ INTO BI GIVING BS REMAINDER BJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY BS ' ' BJ.
           COMPUTE BR ROUNDED = (BA * BB + BI) / 3.
           DISPLAY BR.
           COMPUTE BT ROUNDED = BA * BB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY BT.
           MULTIPLY BA BY BB GIVING BR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY BR.
           COMPUTE BS ROUNDED = BI / BB * BA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY BS.
           MOVE -2540047.19 TO BA. MOVE -123.45 TO BB.
           MOVE -366894049 TO BI. MOVE -9999 TO BJ.
           DIVIDE BB INTO BA GIVING BR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY BR.
           DIVIDE BA BY 3 GIVING BR ROUNDED.
           DISPLAY BR.
           DIVIDE BA BY -7 GIVING BR.
           DISPLAY BR.
           DIVIDE BJ INTO BI GIVING BS REMAINDER BJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY BS ' ' BJ.
           COMPUTE BR ROUNDED = (BA * BB + BI) / 3.
           DISPLAY BR.
           COMPUTE BT ROUNDED = BA * BB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY BT.
           MULTIPLY BA BY BB GIVING BR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY BR.
           COMPUTE BS ROUNDED = BI / BB * BA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY BS.
           MOVE 9999999.99 TO BA. MOVE 95200.6 TO BB.
           MOVE -26030297 TO BI. MOVE 7 TO BJ.
           DIVIDE BB INTO BA GIVING BR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY BR.
           DIVIDE BA BY 3 GIVING BR ROUNDED.
           DISPLAY BR.
           DIVIDE BA BY -7 GIVING BR.
           DISPLAY BR.
           DIVIDE BJ INTO BI GIVING BS REMAINDER BJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY BS ' ' BJ.
           COMPUTE BR ROUNDED = (BA * BB + BI) / 3.
           DISPLAY BR.
           COMPUTE BT ROUNDED = BA * BB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY BT.
           MULTIPLY BA BY BB GIVING BR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY BR.
           COMPUTE BS ROUNDED = BI / BB * BA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY BS.
           MOVE -4179547.24 TO BA. MOVE -1 TO BB.
           MOVE -758278487 TO BI. MOVE 2354 TO BJ.
           DIVIDE BB INTO BA GIVING BR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY BR.
           DIVIDE BA BY 3 GIVING BR ROUNDED.
           DISPLAY BR.
           DIVIDE BA BY -7 GIVING BR.
           DISPLAY BR.
           DIVIDE BJ INTO BI GIVING BS REMAINDER BJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY BS ' ' BJ.
           COMPUTE BR ROUNDED = (BA * BB + BI) / 3.
           DISPLAY BR.
           COMPUTE BT ROUNDED = BA * BB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY BT.
           MULTIPLY BA BY BB GIVING BR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY BR.
           COMPUTE BS ROUNDED = BI / BB * BA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY BS.
           MOVE 2.5 TO BA. MOVE -63975.3 TO BB.
           MOVE 56265651 TO BI. MOVE -1076 TO BJ.
           DIVIDE BB INTO BA GIVING BR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY BR.
           DIVIDE BA BY 3 GIVING BR ROUNDED.
           DISPLAY BR.
           DIVIDE BA BY -7 GIVING BR.
           DISPLAY BR.
           DIVIDE BJ INTO BI GIVING BS REMAINDER BJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY BS ' ' BJ.
           COMPUTE BR ROUNDED = (BA * BB + BI) / 3.
           DISPLAY BR.
           COMPUTE BT ROUNDED = BA * BB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY BT.
           MULTIPLY BA BY BB GIVING BR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY BR.
           COMPUTE BS ROUNDED = BI / BB * BA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY BS.
           MOVE -2.5 TO BA. MOVE 123.45 TO BB.
           MOVE 491635164 TO BI. MOVE 0 TO BJ.
           DIVIDE BB INTO BA GIVING BR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY BR.
           DIVIDE BA BY 3 GIVING BR ROUNDED.
           DISPLAY BR.
           DIVIDE BA BY -7 GIVING BR.
           DISPLAY BR.
           DIVIDE BJ INTO BI GIVING BS REMAINDER BJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY BS ' ' BJ.
           COMPUTE BR ROUNDED = (BA * BB + BI) / 3.
           DISPLAY BR.
           COMPUTE BT ROUNDED = BA * BB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY BT.
           MULTIPLY BA BY BB GIVING BR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY BR.
           COMPUTE BS ROUNDED = BI / BB * BA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY BS.
           MOVE 0.01 TO BA. MOVE 33661.2 TO BB.
           MOVE 793648105 TO BI. MOVE -3 TO BJ.
           DIVIDE BB INTO BA GIVING BR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY BR.
           DIVIDE BA BY 3 GIVING BR ROUNDED.
           DISPLAY BR.
           DIVIDE BA BY -7 GIVING BR.
           DISPLAY BR.
           DIVIDE BJ INTO BI GIVING BS REMAINDER BJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY BS ' ' BJ.
           COMPUTE BR ROUNDED = (BA * BB + BI) / 3.
           DISPLAY BR.
           COMPUTE BT ROUNDED = BA * BB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY BT.
           MULTIPLY BA BY BB GIVING BR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY BR.
           COMPUTE BS ROUNDED = BI / BB * BA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY BS.
           MOVE -0.5 TO BA. MOVE -31985.0 TO BB.
           MOVE -616353358 TO BI. MOVE 0 TO BJ.
           DIVIDE BB INTO BA GIVING BR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY BR.
           DIVIDE BA BY 3 GIVING BR ROUNDED.
           DISPLAY BR.
           DIVIDE BA BY -7 GIVING BR.
           DISPLAY BR.
           DIVIDE BJ INTO BI GIVING BS REMAINDER BJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY BS ' ' BJ.
           COMPUTE BR ROUNDED = (BA * BB + BI) / 3.
           DISPLAY BR.
           COMPUTE BT ROUNDED = BA * BB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY BT.
           MULTIPLY BA BY BB GIVING BR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY BR.
           COMPUTE BS ROUNDED = BI / BB * BA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY BS.
           MOVE 5474339.81 TO BA. MOVE -7.5 TO BB.
           MOVE -5846094 TO BI. MOVE -535 TO BJ.
           DIVIDE BB INTO BA GIVING BR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY BR.
           DIVIDE BA BY 3 GIVING BR ROUNDED.
           DISPLAY BR.
           DIVIDE BA BY -7 GIVING BR.
           DISPLAY BR.
           DIVIDE BJ INTO BI GIVING BS REMAINDER BJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY BS ' ' BJ.
           COMPUTE BR ROUNDED = (BA * BB + BI) / 3.
           DISPLAY BR.
           COMPUTE BT ROUNDED = BA * BB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY BT.
           MULTIPLY BA BY BB GIVING BR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY BR.
           COMPUTE BS ROUNDED = BI / BB * BA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY BS.
           MOVE 0.01 TO PA. MOVE 123.45 TO PB.
           MOVE 584472164 TO PI. MOVE 7 TO PJ.
           DIVIDE PB INTO PA GIVING PR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY PR.
           DIVIDE PA BY 3 GIVING PR ROUNDED.
           DISPLAY PR.
           DIVIDE PA BY -7 GIVING PR.
           DISPLAY PR.
           DIVIDE PJ INTO PI GIVING PS REMAINDER PJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY PS ' ' PJ.
           COMPUTE PR ROUNDED = (PA * PB + PI) / 3.
           DISPLAY PR.
           COMPUTE PT ROUNDED = PA * PB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY PT.
           MULTIPLY PA BY PB GIVING PR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY PR.
           COMPUTE PS ROUNDED = PI / PB * PA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY PS.
           MOVE -1 TO PA. MOVE 37667.0 TO PB.
           MOVE -217726073 TO PI. MOVE 956 TO PJ.
           DIVIDE PB INTO PA GIVING PR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY PR.
           DIVIDE PA BY 3 GIVING PR ROUNDED.
           DISPLAY PR.
           DIVIDE PA BY -7 GIVING PR.
           DISPLAY PR.
           DIVIDE PJ INTO PI GIVING PS REMAINDER PJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY PS ' ' PJ.
           COMPUTE PR ROUNDED = (PA * PB + PI) / 3.
           DISPLAY PR.
           COMPUTE PT ROUNDED = PA * PB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY PT.
           MULTIPLY PA BY PB GIVING PR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY PR.
           COMPUTE PS ROUNDED = PI / PB * PA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY PS.
           MOVE 9604095.48 TO PA. MOVE 123.45 TO PB.
           MOVE 833639203 TO PI. MOVE -3 TO PJ.
           DIVIDE PB INTO PA GIVING PR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY PR.
           DIVIDE PA BY 3 GIVING PR ROUNDED.
           DISPLAY PR.
           DIVIDE PA BY -7 GIVING PR.
           DISPLAY PR.
           DIVIDE PJ INTO PI GIVING PS REMAINDER PJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY PS ' ' PJ.
           COMPUTE PR ROUNDED = (PA * PB + PI) / 3.
           DISPLAY PR.
           COMPUTE PT ROUNDED = PA * PB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY PT.
           MULTIPLY PA BY PB GIVING PR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY PR.
           COMPUTE PS ROUNDED = PI / PB * PA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY PS.
           MOVE -0.01 TO PA. MOVE 99999.9 TO PB.
           MOVE 109079060 TO PI. MOVE -3 TO PJ.
           DIVIDE PB INTO PA GIVING PR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY PR.
           DIVIDE PA BY 3 GIVING PR ROUNDED.
           DISPLAY PR.
           DIVIDE PA BY -7 GIVING PR.
           DISPLAY PR.
           DIVIDE PJ INTO PI GIVING PS REMAINDER PJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY PS ' ' PJ.
           COMPUTE PR ROUNDED = (PA * PB + PI) / 3.
           DISPLAY PR.
           COMPUTE PT ROUNDED = PA * PB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY PT.
           MULTIPLY PA BY PB GIVING PR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY PR.
           COMPUTE PS ROUNDED = PI / PB * PA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY PS.
           MOVE -0.5 TO PA. MOVE -94474.7 TO PB.
           MOVE -656353084 TO PI. MOVE -9999 TO PJ.
           DIVIDE PB INTO PA GIVING PR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY PR.
           DIVIDE PA BY 3 GIVING PR ROUNDED.
           DISPLAY PR.
           DIVIDE PA BY -7 GIVING PR.
           DISPLAY PR.
           DIVIDE PJ INTO PI GIVING PS REMAINDER PJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY PS ' ' PJ.
           COMPUTE PR ROUNDED = (PA * PB + PI) / 3.
           DISPLAY PR.
           COMPUTE PT ROUNDED = PA * PB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY PT.
           MULTIPLY PA BY PB GIVING PR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY PR.
           COMPUTE PS ROUNDED = PI / PB * PA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY PS.
           MOVE -1 TO PA. MOVE -1 TO PB.
           MOVE 354772613 TO PI. MOVE -9999 TO PJ.
           DIVIDE PB INTO PA GIVING PR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY PR.
           DIVIDE PA BY 3 GIVING PR ROUNDED.
           DISPLAY PR.
           DIVIDE PA BY -7 GIVING PR.
           DISPLAY PR.
           DIVIDE PJ INTO PI GIVING PS REMAINDER PJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY PS ' ' PJ.
           COMPUTE PR ROUNDED = (PA * PB + PI) / 3.
           DISPLAY PR.
           COMPUTE PT ROUNDED = PA * PB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY PT.
           MULTIPLY PA BY PB GIVING PR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY PR.
           COMPUTE PS ROUNDED = PI / PB * PA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY PS.
           MOVE -1 TO PA. MOVE -1 TO PB.
           MOVE -761996885 TO PI. MOVE -1 TO PJ.
           DIVIDE PB INTO PA GIVING PR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY PR.
           DIVIDE PA BY 3 GIVING PR ROUNDED.
           DISPLAY PR.
           DIVIDE PA BY -7 GIVING PR.
           DISPLAY PR.
           DIVIDE PJ INTO PI GIVING PS REMAINDER PJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY PS ' ' PJ.
           COMPUTE PR ROUNDED = (PA * PB + PI) / 3.
           DISPLAY PR.
           COMPUTE PT ROUNDED = PA * PB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY PT.
           MULTIPLY PA BY PB GIVING PR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY PR.
           COMPUTE PS ROUNDED = PI / PB * PA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY PS.
           MOVE 966414.72 TO PA. MOVE 123.45 TO PB.
           MOVE -183314438 TO PI. MOVE 3 TO PJ.
           DIVIDE PB INTO PA GIVING PR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY PR.
           DIVIDE PA BY 3 GIVING PR ROUNDED.
           DISPLAY PR.
           DIVIDE PA BY -7 GIVING PR.
           DISPLAY PR.
           DIVIDE PJ INTO PI GIVING PS REMAINDER PJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY PS ' ' PJ.
           COMPUTE PR ROUNDED = (PA * PB + PI) / 3.
           DISPLAY PR.
           COMPUTE PT ROUNDED = PA * PB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY PT.
           MULTIPLY PA BY PB GIVING PR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY PR.
           COMPUTE PS ROUNDED = PI / PB * PA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY PS.
           MOVE 8063645.66 TO PA. MOVE -123.45 TO PB.
           MOVE -401865528 TO PI. MOVE 1 TO PJ.
           DIVIDE PB INTO PA GIVING PR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY PR.
           DIVIDE PA BY 3 GIVING PR ROUNDED.
           DISPLAY PR.
           DIVIDE PA BY -7 GIVING PR.
           DISPLAY PR.
           DIVIDE PJ INTO PI GIVING PS REMAINDER PJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY PS ' ' PJ.
           COMPUTE PR ROUNDED = (PA * PB + PI) / 3.
           DISPLAY PR.
           COMPUTE PT ROUNDED = PA * PB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY PT.
           MULTIPLY PA BY PB GIVING PR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY PR.
           COMPUTE PS ROUNDED = PI / PB * PA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY PS.
           MOVE -2.5 TO PA. MOVE 0 TO PB.
           MOVE 180971974 TO PI. MOVE 1 TO PJ.
           DIVIDE PB INTO PA GIVING PR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY PR.
           DIVIDE PA BY 3 GIVING PR ROUNDED.
           DISPLAY PR.
           DIVIDE PA BY -7 GIVING PR.
           DISPLAY PR.
           DIVIDE PJ INTO PI GIVING PS REMAINDER PJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY PS ' ' PJ.
           COMPUTE PR ROUNDED = (PA * PB + PI) / 3.
           DISPLAY PR.
           COMPUTE PT ROUNDED = PA * PB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY PT.
           MULTIPLY PA BY PB GIVING PR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY PR.
           COMPUTE PS ROUNDED = PI / PB * PA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY PS.
           MOVE 252953.94 TO PA. MOVE -41361.2 TO PB.
           MOVE -244905718 TO PI. MOVE 9999 TO PJ.
           DIVIDE PB INTO PA GIVING PR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY PR.
           DIVIDE PA BY 3 GIVING PR ROUNDED.
           DISPLAY PR.
           DIVIDE PA BY -7 GIVING PR.
           DISPLAY PR.
           DIVIDE PJ INTO PI GIVING PS REMAINDER PJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY PS ' ' PJ.
           COMPUTE PR ROUNDED = (PA * PB + PI) / 3.
           DISPLAY PR.
           COMPUTE PT ROUNDED = PA * PB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY PT.
           MULTIPLY PA BY PB GIVING PR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY PR.
           COMPUTE PS ROUNDED = PI / PB * PA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY PS.
           MOVE 1278402.48 TO PA. MOVE -6505.5 TO PB.
           MOVE -663327281 TO PI. MOVE 9999 TO PJ.
           DIVIDE PB INTO PA GIVING PR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY PR.
           DIVIDE PA BY 3 GIVING PR ROUNDED.
           DISPLAY PR.
           DIVIDE PA BY -7 GIVING PR.
           DISPLAY PR.
           DIVIDE PJ INTO PI GIVING PS REMAINDER PJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY PS ' ' PJ.
           COMPUTE PR ROUNDED = (PA * PB + PI) / 3.
           DISPLAY PR.
           COMPUTE PT ROUNDED = PA * PB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY PT.
           MULTIPLY PA BY PB GIVING PR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY PR.
           COMPUTE PS ROUNDED = PI / PB * PA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY PS.
           MOVE 0.5 TO PA. MOVE 21721.8 TO PB.
           MOVE -245983956 TO PI. MOVE 9999 TO PJ.
           DIVIDE PB INTO PA GIVING PR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY PR.
           DIVIDE PA BY 3 GIVING PR ROUNDED.
           DISPLAY PR.
           DIVIDE PA BY -7 GIVING PR.
           DISPLAY PR.
           DIVIDE PJ INTO PI GIVING PS REMAINDER PJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY PS ' ' PJ.
           COMPUTE PR ROUNDED = (PA * PB + PI) / 3.
           DISPLAY PR.
           COMPUTE PT ROUNDED = PA * PB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY PT.
           MULTIPLY PA BY PB GIVING PR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY PR.
           COMPUTE PS ROUNDED = PI / PB * PA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY PS.
           MOVE 1316240.14 TO PA. MOVE -71859.2 TO PB.
           MOVE 351747090 TO PI. MOVE 9999 TO PJ.
           DIVIDE PB INTO PA GIVING PR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY PR.
           DIVIDE PA BY 3 GIVING PR ROUNDED.
           DISPLAY PR.
           DIVIDE PA BY -7 GIVING PR.
           DISPLAY PR.
           DIVIDE PJ INTO PI GIVING PS REMAINDER PJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY PS ' ' PJ.
           COMPUTE PR ROUNDED = (PA * PB + PI) / 3.
           DISPLAY PR.
           COMPUTE PT ROUNDED = PA * PB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY PT.
           MULTIPLY PA BY PB GIVING PR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY PR.
           COMPUTE PS ROUNDED = PI / PB * PA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY PS.
           MOVE 9533607.64 TO NA. MOVE 0.05 TO NB.
           MOVE 521059528 TO NI. MOVE 9999 TO NJ.
           DIVIDE NB INTO NA GIVING NR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY NR.
           DIVIDE NA BY 3 GIVING NR ROUNDED.
           DISPLAY NR.
           DIVIDE NA BY -7 GIVING NR.
           DISPLAY NR.
           DIVIDE NJ INTO NI GIVING NS REMAINDER NJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY NS ' ' NJ.
           COMPUTE NR ROUNDED = (NA * NB + NI) / 3.
           DISPLAY NR.
           COMPUTE NT ROUNDED = NA * NB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY NT.
           MULTIPLY NA BY NB GIVING NR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY NR.
           COMPUTE NS ROUNDED = NI / NB * NA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY NS.
           MOVE -1 TO NA. MOVE -48034.6 TO NB.
           MOVE -354031115 TO NI. MOVE -3 TO NJ.
           DIVIDE NB INTO NA GIVING NR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY NR.
           DIVIDE NA BY 3 GIVING NR ROUNDED.
           DISPLAY NR.
           DIVIDE NA BY -7 GIVING NR.
           DISPLAY NR.
           DIVIDE NJ INTO NI GIVING NS REMAINDER NJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY NS ' ' NJ.
           COMPUTE NR ROUNDED = (NA * NB + NI) / 3.
           DISPLAY NR.
           COMPUTE NT ROUNDED = NA * NB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY NT.
           MULTIPLY NA BY NB GIVING NR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY NR.
           COMPUTE NS ROUNDED = NI / NB * NA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY NS.
           MOVE -3616948.67 TO NA. MOVE -1649.0 TO NB.
           MOVE -384964221 TO NI. MOVE -9999 TO NJ.
           DIVIDE NB INTO NA GIVING NR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY NR.
           DIVIDE NA BY 3 GIVING NR ROUNDED.
           DISPLAY NR.
           DIVIDE NA BY -7 GIVING NR.
           DISPLAY NR.
           DIVIDE NJ INTO NI GIVING NS REMAINDER NJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY NS ' ' NJ.
           COMPUTE NR ROUNDED = (NA * NB + NI) / 3.
           DISPLAY NR.
           COMPUTE NT ROUNDED = NA * NB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY NT.
           MULTIPLY NA BY NB GIVING NR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY NR.
           COMPUTE NS ROUNDED = NI / NB * NA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY NS.
           MOVE 3201176.59 TO NA. MOVE -50899.4 TO NB.
           MOVE -914151118 TO NI. MOVE 9999 TO NJ.
           DIVIDE NB INTO NA GIVING NR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY NR.
           DIVIDE NA BY 3 GIVING NR ROUNDED.
           DISPLAY NR.
           DIVIDE NA BY -7 GIVING NR.
           DISPLAY NR.
           DIVIDE NJ INTO NI GIVING NS REMAINDER NJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY NS ' ' NJ.
           COMPUTE NR ROUNDED = (NA * NB + NI) / 3.
           DISPLAY NR.
           COMPUTE NT ROUNDED = NA * NB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY NT.
           MULTIPLY NA BY NB GIVING NR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY NR.
           COMPUTE NS ROUNDED = NI / NB * NA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY NS.
           MOVE 651292.97 TO NA. MOVE 0.05 TO NB.
           MOVE 679133012 TO NI. MOVE 3 TO NJ.
           DIVIDE NB INTO NA GIVING NR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY NR.
           DIVIDE NA BY 3 GIVING NR ROUNDED.
           DISPLAY NR.
           DIVIDE NA BY -7 GIVING NR.
           DISPLAY NR.
           DIVIDE NJ INTO NI GIVING NS REMAINDER NJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY NS ' ' NJ.
           COMPUTE NR ROUNDED = (NA * NB + NI) / 3.
           DISPLAY NR.
           COMPUTE NT ROUNDED = NA * NB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY NT.
           MULTIPLY NA BY NB GIVING NR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY NR.
           COMPUTE NS ROUNDED = NI / NB * NA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY NS.
           MOVE 6878278.78 TO NA. MOVE -42853.8 TO NB.
           MOVE 57128805 TO NI. MOVE -9999 TO NJ.
           DIVIDE NB INTO NA GIVING NR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY NR.
           DIVIDE NA BY 3 GIVING NR ROUNDED.
           DISPLAY NR.
           DIVIDE NA BY -7 GIVING NR.
           DISPLAY NR.
           DIVIDE NJ INTO NI GIVING NS REMAINDER NJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY NS ' ' NJ.
           COMPUTE NR ROUNDED = (NA * NB + NI) / 3.
           DISPLAY NR.
           COMPUTE NT ROUNDED = NA * NB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY NT.
           MULTIPLY NA BY NB GIVING NR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY NR.
           COMPUTE NS ROUNDED = NI / NB * NA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY NS.
           MOVE 9999999.99 TO NA. MOVE 78999.8 TO NB.
           MOVE -965269262 TO NI. MOVE -3 TO NJ.
           DIVIDE NB INTO NA GIVING NR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY NR.
           DIVIDE NA BY 3 GIVING NR ROUNDED.
           DISPLAY NR.
           DIVIDE NA BY -7 GIVING NR.
           DISPLAY NR.
           DIVIDE NJ INTO NI GIVING NS REMAINDER NJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY NS ' ' NJ.
           COMPUTE NR ROUNDED = (NA * NB + NI) / 3.
           DISPLAY NR.
           COMPUTE NT ROUNDED = NA * NB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY NT.
           MULTIPLY NA BY NB GIVING NR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY NR.
           COMPUTE NS ROUNDED = NI / NB * NA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY NS.
           MOVE 4160545.55 TO NA. MOVE 46254.7 TO NB.
           MOVE -942842935 TO NI. MOVE 7 TO NJ.
           DIVIDE NB INTO NA GIVING NR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY NR.
           DIVIDE NA BY 3 GIVING NR ROUNDED.
           DISPLAY NR.
           DIVIDE NA BY -7 GIVING NR.
           DISPLAY NR.
           DIVIDE NJ INTO NI GIVING NS REMAINDER NJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY NS ' ' NJ.
           COMPUTE NR ROUNDED = (NA * NB + NI) / 3.
           DISPLAY NR.
           COMPUTE NT ROUNDED = NA * NB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY NT.
           MULTIPLY NA BY NB GIVING NR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY NR.
           COMPUTE NS ROUNDED = NI / NB * NA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY NS.
           MOVE 4900343.2 TO NA. MOVE -92713.3 TO NB.
           MOVE -457925843 TO NI. MOVE -9999 TO NJ.
           DIVIDE NB INTO NA GIVING NR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY NR.
           DIVIDE NA BY 3 GIVING NR ROUNDED.
           DISPLAY NR.
           DIVIDE NA BY -7 GIVING NR.
           DISPLAY NR.
           DIVIDE NJ INTO NI GIVING NS REMAINDER NJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY NS ' ' NJ.
           COMPUTE NR ROUNDED = (NA * NB + NI) / 3.
           DISPLAY NR.
           COMPUTE NT ROUNDED = NA * NB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY NT.
           MULTIPLY NA BY NB GIVING NR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY NR.
           COMPUTE NS ROUNDED = NI / NB * NA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY NS.
           MOVE 1 TO NA. MOVE 9118.4 TO NB.
           MOVE -968731525 TO NI. MOVE 9999 TO NJ.
           DIVIDE NB INTO NA GIVING NR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY NR.
           DIVIDE NA BY 3 GIVING NR ROUNDED.
           DISPLAY NR.
           DIVIDE NA BY -7 GIVING NR.
           DISPLAY NR.
           DIVIDE NJ INTO NI GIVING NS REMAINDER NJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY NS ' ' NJ.
           COMPUTE NR ROUNDED = (NA * NB + NI) / 3.
           DISPLAY NR.
           COMPUTE NT ROUNDED = NA * NB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY NT.
           MULTIPLY NA BY NB GIVING NR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY NR.
           COMPUTE NS ROUNDED = NI / NB * NA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY NS.
           MOVE 2.5 TO NA. MOVE 68239.4 TO NB.
           MOVE 21600329 TO NI. MOVE 3 TO NJ.
           DIVIDE NB INTO NA GIVING NR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY NR.
           DIVIDE NA BY 3 GIVING NR ROUNDED.
           DISPLAY NR.
           DIVIDE NA BY -7 GIVING NR.
           DISPLAY NR.
           DIVIDE NJ INTO NI GIVING NS REMAINDER NJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY NS ' ' NJ.
           COMPUTE NR ROUNDED = (NA * NB + NI) / 3.
           DISPLAY NR.
           COMPUTE NT ROUNDED = NA * NB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY NT.
           MULTIPLY NA BY NB GIVING NR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY NR.
           COMPUTE NS ROUNDED = NI / NB * NA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY NS.
           MOVE 2.5 TO NA. MOVE -86788.3 TO NB.
           MOVE 795894696 TO NI. MOVE 9999 TO NJ.
           DIVIDE NB INTO NA GIVING NR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY NR.
           DIVIDE NA BY 3 GIVING NR ROUNDED.
           DISPLAY NR.
           DIVIDE NA BY -7 GIVING NR.
           DISPLAY NR.
           DIVIDE NJ INTO NI GIVING NS REMAINDER NJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY NS ' ' NJ.
           COMPUTE NR ROUNDED = (NA * NB + NI) / 3.
           DISPLAY NR.
           COMPUTE NT ROUNDED = NA * NB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY NT.
           MULTIPLY NA BY NB GIVING NR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY NR.
           COMPUTE NS ROUNDED = NI / NB * NA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY NS.
           MOVE 2.5 TO NA. MOVE -71462.4 TO NB.
           MOVE 27383264 TO NI. MOVE -9999 TO NJ.
           DIVIDE NB INTO NA GIVING NR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY NR.
           DIVIDE NA BY 3 GIVING NR ROUNDED.
           DISPLAY NR.
           DIVIDE NA BY -7 GIVING NR.
           DISPLAY NR.
           DIVIDE NJ INTO NI GIVING NS REMAINDER NJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY NS ' ' NJ.
           COMPUTE NR ROUNDED = (NA * NB + NI) / 3.
           DISPLAY NR.
           COMPUTE NT ROUNDED = NA * NB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY NT.
           MULTIPLY NA BY NB GIVING NR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY NR.
           COMPUTE NS ROUNDED = NI / NB * NA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY NS.
           MOVE -2.5 TO NA. MOVE 99999.9 TO NB.
           MOVE 162661021 TO NI. MOVE 1 TO NJ.
           DIVIDE NB INTO NA GIVING NR ROUNDED
               ON SIZE ERROR DISPLAY 'SE1'.
           DISPLAY NR.
           DIVIDE NA BY 3 GIVING NR ROUNDED.
           DISPLAY NR.
           DIVIDE NA BY -7 GIVING NR.
           DISPLAY NR.
           DIVIDE NJ INTO NI GIVING NS REMAINDER NJ
               ON SIZE ERROR DISPLAY 'SE2'.
           DISPLAY NS ' ' NJ.
           COMPUTE NR ROUNDED = (NA * NB + NI) / 3.
           DISPLAY NR.
           COMPUTE NT ROUNDED = NA * NB / 1000
               ON SIZE ERROR DISPLAY 'SE3'.
           DISPLAY NT.
           MULTIPLY NA BY NB GIVING NR ROUNDED
               ON SIZE ERROR DISPLAY 'SE4'.
           DISPLAY NR.
           COMPUTE NS ROUNDED = NI / NB * NA
               ON SIZE ERROR DISPLAY 'SE5'.
           DISPLAY NS.
           MOVE 99999999999999999 TO BIG.
           COMPUTE RES = BIG * 100 ON SIZE ERROR DISPLAY 'SE6'.
           DISPLAY RES.
           COMPUTE RES = BIG / 7 * 3 ON SIZE ERROR DISPLAY 'SE7'.
           DISPLAY RES.
           MOVE -99999999999999999 TO BIG.
           COMPUTE RES = BIG * 100 ON SIZE ERROR DISPLAY 'SE6'.
           DISPLAY RES.
           COMPUTE RES = BIG / 7 * 3 ON SIZE ERROR DISPLAY 'SE7'.
           DISPLAY RES.
           MOVE 92233720368547 TO BIG.
           COMPUTE RES = BIG * 100 ON SIZE ERROR DISPLAY 'SE6'.
           DISPLAY RES.
           COMPUTE RES = BIG / 7 * 3 ON SIZE ERROR DISPLAY 'SE7'.
           DISPLAY RES.
           MOVE 123 TO BIG.
           COMPUTE RES = BIG * 100 ON SIZE ERROR DISPLAY 'SE6'.
           DISPLAY RES.
           COMPUTE RES = BIG / 7 * 3 ON SIZE ERROR DISPLAY 'SE7'.
           DISPLAY RES.
           MOVE 99999999999999999 TO W17 W5. MOVE 3 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 99999999999999999 TO W17 W5. MOVE -7 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 99999999999999999 TO W17 W5. MOVE 1 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 99999999999999999 TO W17 W5. MOVE 964540104 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 99999999999999999 TO W17 W5. MOVE -649290420 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 99999999999999999 TO W17 W5. MOVE 0 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE -99999999999999999 TO W17 W5. MOVE 3 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE -99999999999999999 TO W17 W5. MOVE -7 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE -99999999999999999 TO W17 W5. MOVE 1 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE -99999999999999999 TO W17 W5. MOVE 86774070 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE -99999999999999999 TO W17 W5. MOVE -358655545 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE -99999999999999999 TO W17 W5. MOVE 0 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 92233720368547758 TO W17 W5. MOVE 3 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 92233720368547758 TO W17 W5. MOVE -7 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 92233720368547758 TO W17 W5. MOVE 1 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 92233720368547758 TO W17 W5. MOVE 595143378 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 92233720368547758 TO W17 W5. MOVE -662122943 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 92233720368547758 TO W17 W5. MOVE 0 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 12345678901234567 TO W17 W5. MOVE 3 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 12345678901234567 TO W17 W5. MOVE -7 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 12345678901234567 TO W17 W5. MOVE 1 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 12345678901234567 TO W17 W5. MOVE 751724830 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 12345678901234567 TO W17 W5. MOVE -43911492 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 12345678901234567 TO W17 W5. MOVE 0 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE -3 TO W17 W5. MOVE 3 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE -3 TO W17 W5. MOVE -7 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE -3 TO W17 W5. MOVE 1 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE -3 TO W17 W5. MOVE 781890460 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE -3 TO W17 W5. MOVE -406969201 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE -3 TO W17 W5. MOVE 0 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 0 TO W17 W5. MOVE 3 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 0 TO W17 W5. MOVE -7 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 0 TO W17 W5. MOVE 1 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 0 TO W17 W5. MOVE 181907113 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 0 TO W17 W5. MOVE -755356314 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 0 TO W17 W5. MOVE 0 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 1 TO W17 W5. MOVE 3 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 1 TO W17 W5. MOVE -7 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 1 TO W17 W5. MOVE 1 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 1 TO W17 W5. MOVE 485353765 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 1 TO W17 W5. MOVE -778533014 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 1 TO W17 W5. MOVE 0 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 76747256362804847 TO W17 W5. MOVE 3 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 76747256362804847 TO W17 W5. MOVE -7 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 76747256362804847 TO W17 W5. MOVE 1 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 76747256362804847 TO W17 W5. MOVE 453748998 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 76747256362804847 TO W17 W5. MOVE -168746218 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 76747256362804847 TO W17 W5. MOVE 0 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE -23003746019090703 TO W17 W5. MOVE 3 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE -23003746019090703 TO W17 W5. MOVE -7 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE -23003746019090703 TO W17 W5. MOVE 1 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE -23003746019090703 TO W17 W5. MOVE 180848162 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE -23003746019090703 TO W17 W5. MOVE -255563660 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE -23003746019090703 TO W17 W5. MOVE 0 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE -46347371022560853 TO W17 W5. MOVE 3 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE -46347371022560853 TO W17 W5. MOVE -7 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE -46347371022560853 TO W17 W5. MOVE 1 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE -46347371022560853 TO W17 W5. MOVE 55119494 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE -46347371022560853 TO W17 W5. MOVE -119129003 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE -46347371022560853 TO W17 W5. MOVE 0 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 95029233068080982 TO W17 W5. MOVE 3 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 95029233068080982 TO W17 W5. MOVE -7 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 95029233068080982 TO W17 W5. MOVE 1 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 95029233068080982 TO W17 W5. MOVE 142190876 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 95029233068080982 TO W17 W5. MOVE -543602255 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 95029233068080982 TO W17 W5. MOVE 0 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE -2484313704908207 TO W17 W5. MOVE 3 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE -2484313704908207 TO W17 W5. MOVE -7 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE -2484313704908207 TO W17 W5. MOVE 1 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE -2484313704908207 TO W17 W5. MOVE 936008045 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE -2484313704908207 TO W17 W5. MOVE -991954680 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE -2484313704908207 TO W17 W5. MOVE 0 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 33658632791675783 TO W17 W5. MOVE 3 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 33658632791675783 TO W17 W5. MOVE -7 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 33658632791675783 TO W17 W5. MOVE 1 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 33658632791675783 TO W17 W5. MOVE 633967735 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 33658632791675783 TO W17 W5. MOVE -67997464 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 33658632791675783 TO W17 W5. MOVE 0 TO WD.
           COMPUTE WQ ROUNDED = W17 / 3
               ON SIZE ERROR DISPLAY 'SW1'.
           DISPLAY WQ.
           COMPUTE WQ = W5 / WD ON SIZE ERROR DISPLAY 'SW2'.
           DISPLAY WQ.
           COMPUTE WV ROUNDED = (W17 + W5) / -7
               ON SIZE ERROR DISPLAY 'SW3'.
           DISPLAY WV.
           DIVIDE WD INTO W17 GIVING WV ROUNDED
               ON SIZE ERROR DISPLAY 'SW4'.
           DISPLAY WV.
           MOVE 4294967296 TO M1. MOVE 2147483648 TO M2.
           COMPUTE M3 = M1 * M2 ON SIZE ERROR DISPLAY 'SM1'.
           DISPLAY M3.
           COMPUTE MV = M1 * M2 / 1000
               ON SIZE ERROR DISPLAY 'SM2'.
           DISPLAY MV.
           MOVE M1 TO MV.
           COMPUTE MV ROUNDED = MV * MV ON SIZE ERROR DISPLAY 'SM3'.
           DISPLAY MV.
           MOVE -4294967296 TO M1. MOVE 2147483648 TO M2.
           COMPUTE M3 = M1 * M2 ON SIZE ERROR DISPLAY 'SM1'.
           DISPLAY M3.
           COMPUTE MV = M1 * M2 / 1000
               ON SIZE ERROR DISPLAY 'SM2'.
           DISPLAY MV.
           MOVE M1 TO MV.
           COMPUTE MV ROUNDED = MV * MV ON SIZE ERROR DISPLAY 'SM3'.
           DISPLAY MV.
           MOVE 3037000499 TO M1. MOVE 3037000499 TO M2.
           COMPUTE M3 = M1 * M2 ON SIZE ERROR DISPLAY 'SM1'.
           DISPLAY M3.
           COMPUTE MV = M1 * M2 / 1000
               ON SIZE ERROR DISPLAY 'SM2'.
           DISPLAY MV.
           MOVE M1 TO MV.
           COMPUTE MV ROUNDED = MV * MV ON SIZE ERROR DISPLAY 'SM3'.
           DISPLAY MV.
           MOVE 3037000500 TO M1. MOVE 3037000500 TO M2.
           COMPUTE M3 = M1 * M2 ON SIZE ERROR DISPLAY 'SM1'.
           DISPLAY M3.
           COMPUTE MV = M1 * M2 / 1000
               ON SIZE ERROR DISPLAY 'SM2'.
           DISPLAY MV.
           MOVE M1 TO MV.
           COMPUTE MV ROUNDED = MV * MV ON SIZE ERROR DISPLAY 'SM3'.
           DISPLAY MV.
           MOVE -3037000500 TO M1. MOVE 3037000500 TO M2.
           COMPUTE M3 = M1 * M2 ON SIZE ERROR DISPLAY 'SM1'.
           DISPLAY M3.
           COMPUTE MV = M1 * M2 / 1000
               ON SIZE ERROR DISPLAY 'SM2'.
           DISPLAY MV.
           MOVE M1 TO MV.
           COMPUTE MV ROUNDED = MV * MV ON SIZE ERROR DISPLAY 'SM3'.
           DISPLAY MV.
           MOVE 999999999999999999 TO M1. MOVE 1 TO M2.
           COMPUTE M3 = M1 * M2 ON SIZE ERROR DISPLAY 'SM1'.
           DISPLAY M3.
           COMPUTE MV = M1 * M2 / 1000
               ON SIZE ERROR DISPLAY 'SM2'.
           DISPLAY MV.
           MOVE M1 TO MV.
           COMPUTE MV ROUNDED = MV * MV ON SIZE ERROR DISPLAY 'SM3'.
           DISPLAY MV.
           MOVE 999999999999999999 TO M1. MOVE -1 TO M2.
           COMPUTE M3 = M1 * M2 ON SIZE ERROR DISPLAY 'SM1'.
           DISPLAY M3.
           COMPUTE MV = M1 * M2 / 1000
               ON SIZE ERROR DISPLAY 'SM2'.
           DISPLAY MV.
           MOVE M1 TO MV.
           COMPUTE MV ROUNDED = MV * MV ON SIZE ERROR DISPLAY 'SM3'.
           DISPLAY MV.
           MOVE 123456789 TO M1. MOVE -987654321 TO M2.
           COMPUTE M3 = M1 * M2 ON SIZE ERROR DISPLAY 'SM1'.
           DISPLAY M3.
           COMPUTE MV = M1 * M2 / 1000
               ON SIZE ERROR DISPLAY 'SM2'.
           DISPLAY MV.
           MOVE M1 TO MV.
           COMPUTE MV ROUNDED = MV * MV ON SIZE ERROR DISPLAY 'SM3'.
           DISPLAY MV.
           MOVE 0 TO M1. MOVE -5 TO M2.
           COMPUTE M3 = M1 * M2 ON SIZE ERROR DISPLAY 'SM1'.
           DISPLAY M3.
           COMPUTE MV = M1 * M2 / 1000
               ON SIZE ERROR DISPLAY 'SM2'.
           DISPLAY MV.
           MOVE M1 TO MV.
           COMPUTE MV ROUNDED = MV * MV ON SIZE ERROR DISPLAY 'SM3'.
           DISPLAY MV.
           STOP RUN.
