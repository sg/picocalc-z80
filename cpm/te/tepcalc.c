/*	tepcalc.c

	Text editor -- version for picocalc-z80, z80pack on the ClockworkPi
	PicoCalc.

	Copyright (c) 2015-2021 Miguel Garcia / FloppySoftware

	This program is free software; you can redistribute it and/or modify it
	under the terms of the GNU General Public License as published by the
	Free Software Foundation; either version 2, or (at your option) any
	later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

	Usage:

	te [filename]

	Compilation:

	cc tepcalc -e-
	ccopt tepcalc
	zsm tepcalc
	hextocom tepcalc
	tecf patch tepcalc.com tepcalc.cf

	Changes:

	16 Oct 2026 : 1st version, from te_ansi.c.

	Notes:

	The PicoCalc console is an ANSI terminal: 64 columns by 32 rows with
	the 5x9 font and the status block on, which TEPCALC.CF records.

	Its keyboard sends the arrow, Home, End, PgUp, PgDn and Del keys
	either as ANSI sequences or as WordStar control keys, depending on
	the K setting of the machine's config dialog. This adaptation binds
	the WordStar keys and turns the ANSI sequences into them, so that
	both settings work the same. Home and End become two keys, ^QS and
	^QD, so one character is kept back for the next call.

	Console I/O goes straight to the BIOS, as in te_ansi.c, which keeps
	the BDOS from looking for ^S in the output.
*/

/* Default configuration values
   ----------------------------
*/
#define CRT_DEF_ROWS 32
#define CRT_DEF_COLS 64

/* Options
   -------
   Set to 1 to add the following functionalities, else 0.
*/
#define OPT_LWORD 1  /* Go to word on the left */
#define OPT_RWORD 1  /* Go to word on the right */
#define OPT_FIND  1  /* Find string */
#define OPT_GOTO  1  /* Go to line # */
#define OPT_BLOCK 1  /* Block selection */
#define OPT_MACRO 1  /* Enable macros */

/* Include main code
   -----------------
*/
#include "te.c"

/* The second key of a translated two-key sequence
   -----------------------------------------------
*/
int crt_pend;

/* Setup CRT: Used when the editor starts
   --------------------------------------
   void CrtSetup(void)
*/
CrtSetup()
{
	crt_pend = 0;

	CrtSetupEx();
}

#asm
CrtSetupEx:
	ld  hl,(1)
	inc hl
	inc hl
	inc hl
	ld  de,BiosConst
	ld  bc,9
	ldir
	ret

BiosConst:  jp 0
BiosConin:  jp 0
BiosConout: jp 0
#endasm

/* Reset CRT: Used when the editor exits
   -------------------------------------
   void CrtReset(void)
*/
CrtReset()
{
}

/* Output character to the CRT
   ---------------------------
   All program output is done with this function.

   On '\n' outputs '\n' + '\r'.

   void CrtOut(int ch)
*/
#asm
CrtOut:
	ld   c,l
	ld   a,l
	cp   10
	jp   nz,BiosConout
	call BiosConout
	ld   c,13
	jp   BiosConout
#endasm

/* Input character from the keyboard
   ---------------------------------
   All program input is done with this function.

   Translates the ANSI key codes into the WordStar keys of TEPCALC.CF:

   UP:    ESC [ A   --> ^E
   DOWN:  ESC [ B   --> ^X
   RIGHT: ESC [ C   --> ^D
   LEFT:  ESC [ D   --> ^S
   HOME:  ESC [ H   --> ^Q S
   END:   ESC [ F   --> ^Q D
   PGUP:  ESC [ 5 ~ --> ^R
   PGDN:  ESC [ 6 ~ --> ^C
   DEL:   DEL       --> ^G

   int CrtIn(void)
*/
CrtIn()
{
	int ch;

	if(crt_pend)
	{
		ch = crt_pend; crt_pend = 0;

		return ch;
	}

	ch = CrtInEx();

	if(ch == DEL)
	{
		return CTL_G;
	}

	if(ch == ESC && CrtInSt())
	{
		if(CrtInEx() == '[' && CrtInSt())
		{
			switch(CrtInEx())
			{
				case 'A' : /* UP */
					return CTL_E;
				case 'B' : /* DOWN */
					return CTL_X;
				case 'C' : /* RIGHT */
					return CTL_D;
				case 'D' : /* LEFT */
					return CTL_S;
				case 'H' : /* HOME */
					crt_pend = 'S';
					return CTL_Q;
				case 'F' : /* END */
					crt_pend = 'D';
					return CTL_Q;
				case '5' : /* PGUP */
					if(CrtInTilde())
					{
						return CTL_R;
					}
					break;
				case '6' : /* PGDN */
					if(CrtInTilde())
					{
						return CTL_C;
					}
					break;
				case '2' : /* INSERT: nothing to do */
					CrtInTilde();
					break;
			}
		}

		/* An unknown sequence is swallowed rather than typed */
		return 0;
	}

	return ch;
}

/* Eat the ~ that ends a numbered sequence
   ---------------------------------------
   int CrtInTilde(void)
*/
CrtInTilde()
{
	return CrtInSt() && CrtInEx() == '~';
}

#asm
CrtInSt:
	ld hl,8200

CrtInSt0:
	dec hl

	ld a,h
	or l

	jr nz,CrtInSt0

	call BiosConst
	ld h,0
	ld l,a
	ret

CrtInEx:
	call BiosConin
	ld h,0
	ld l,a
	ret
#endasm

/* Clear screen and send cursor to 0,0
   -----------------------------------
   void CrtClear(void)
*/
CrtClear()
{
	CrtOut(27); putstr("[1;1H"); /* Cursor to 0,0 */
	CrtOut(27); putstr("[2J");   /* Clear CRT */
}

/* Locate the cursor (HOME is 0,0)
   -------------------------------
   void CrtLocate(int row, int col)
*/
CrtLocate(row, col)
int row, col;
{
	CrtOut(27); CrtOut('[');
	putint("%d", row + 1); CrtOut(';');
	putint("%d", col + 1); CrtOut('H');
}

/* Erase line and cursor to row,0
   ------------------------------
   void CrtClearLine(int row)
*/
CrtClearLine(row)
int row;
{
	CrtLocate(row, 0); CrtClearEol();
}

/* Erase from the cursor to the end of the line
   --------------------------------------------
*/
CrtClearEol()
{
	CrtOut(27); putstr("[K");
}

/* Turn on / off reverse video
   ---------------------------
*/
CrtReverse(on)
int on;
{
	CrtOut(27); CrtOut('['); CrtOut(on ? '7' : '0'); CrtOut('m');
}
