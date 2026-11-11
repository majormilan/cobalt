      * TAXCALC: the tax on a gross pay, by bands: nothing up to 500.00,
      * 20% of the part up to 2000.00, 40% of the rest
       IDENTIFICATION DIVISION.
       PROGRAM-ID. TAXCALC.
       DATA DIVISION.
       LINKAGE SECTION.
       01  GROSS-PAY           PIC 9(6)V99.
       01  TAX-DUE             PIC 9(6)V99.
       PROCEDURE DIVISION USING GROSS-PAY TAX-DUE.
           EVALUATE TRUE
               WHEN GROSS-PAY <= 500
                   MOVE 0 TO TAX-DUE
               WHEN GROSS-PAY <= 2000
                   COMPUTE TAX-DUE ROUNDED = (GROSS-PAY - 500) * 0.20
               WHEN OTHER
                   COMPUTE TAX-DUE ROUNDED =
                       1500 * 0.20 + (GROSS-PAY - 2000) * 0.40
           END-EVALUATE.
           EXIT PROGRAM.
