      * A record description with a mistake in it
       01  BAD-RECORD.
           05  BAD-CODE       PIC X(3) VALUE "TOO LONG".
           05  BAD-COUNT      PIC 9(4).
           COPY INNER.
