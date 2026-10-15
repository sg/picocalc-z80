picocalc-z80 comms disk
=======================

Udo Munk's IMSAI 8080 comms disk from z80pack, set up for the
PicoCalc. The modem is IMSAI SIO-2 port B (24/25h) and only
exists on the WiFi boards; the USB-C serial line is port A
(22/23h). Join a network from the setup menu (n) or with
AT+WIFI first, then dial with ATDT host[:port].

QT.COM          QTerm, talking to the modem
QT-IMSAI.COM    QTerm as on the IMSAI, talking to port A
                ^^ is the escape key, ^^? lists the commands,
                ^^U switches channel (A USB-C, B modem),
                ^^, hangs up, ^^R/^^S receive/send a file
                and ^^Q quits, leaving any call up
KERMIT.COM      Kermit-80 4.11 for CP/M 2.2:
                    SET PORT UC1
                    CONNECT
                ^\C returns to the Kermit prompt
KERMIT3.COM     Kermit-80 for CP/M 3, talks to AUX:
XMODEM.COM      XMODEM 2.x; XMODEM.CFG here uses the modem.
                XMODEM /X1 uses RDR:/PUN: instead

The BIOS routes the CP/M devices like z80pack's IMSAI:

    CON: UC1: and RDR: UR1:, PUN: UP1: are the modem
    RDR: PTR:, PUN: PTP: and LST: UL1: are USB-C
    TTY:, CRT:, UR2: and UP2: are the PicoCalc console

RDR: and PUN: start out as USB-C. To move them to the modem,
for PIP or XMODEM /X1:

    STAT RDR:=UR1:
    STAT PUN:=UP1:

QTerm, Kermit and XMODEM are freeware or public domain; see
QTERM.DOC and the z80pack sources for their documentation.
