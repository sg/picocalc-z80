;	8080 CBIOS for z80pack machines using SD-FDC
;
;	Copyright (C) 2024 by Udo Munk
;
;	PicoCalc: added a 4 MB hard disk as drive I:. The controller already
;	carries a 4 bit drive number and byte wide track and sector, so no
;	new hardware is needed - drive 8 simply has a different geometry,
;	256 tracks of 128 sectors, and its own disk parameter block.
;
;	PicoCalc: the character devices follow the IOBYTE, with the same
;	assignments as z80pack's IMSAI BIOS so the Kermit, QTerm and XMODEM
;	setups from its comms disk work unchanged:
;
;	CON:	TTY: CRT: = console		BAT: = RDR: in, LST: out
;		UC1: = modem
;	RDR:	TTY: UR2: = console		PTR: = USB-C serial
;		UR1: = modem
;	PUN:	TTY: UP2: = console		PTP: = USB-C serial
;		UP1: = modem
;	LST:	TTY: CRT: = console		LPT: = none
;		UL1: = USB-C serial
;
MSIZE	EQU	64		;CP/M memory size in kilobytes
;
;	"bias" is address offset from 3400H for memory systems
;	than 16K (referred to as "b" throughout the text).
;
BIAS	EQU	(MSIZE-20)*1024
CCP	EQU	3400H+BIAS	;base of ccp
BDOS	EQU	CCP+806H	;base of bdos
BIOS	EQU	CCP+1600H	;base of bios
NSECTS	EQU	(BIOS-CCP)/128	;warm start sector count
CDISK	EQU	0004H		;current disk number 0=A,...,15=P
IOBYTE	EQU	0003H		;Intel I/O byte
INITIO	EQU	10010100B	;CON:=TTY:, RDR:=PTR:, PUN:=PTP:, LST:=LPT:
FDCCMD	EQU	0040H		;FDC command bytes
DDTRK	EQU	0		;offset for track
DDSEC	EQU	1		;offset for sector
DDLDMA	EQU	2		;offset for DMA address low
DDHDMA	EQU	3		;offset for DMA address high
;
;	I/O ports
;
CONSTA	EQU	0		;console status port
CONDAT	EQU	1		;console data port
FDC	EQU	4		;port for the FDC
TTY2	EQU	22H		;SIO-2 port A data, the USB-C serial line
TTY2S	EQU	23H		;SIO-2 port A status
MODEM	EQU	24H		;SIO-2 port B data, the WiFi modem
MODEMS	EQU	25H		;SIO-2 port B status
;
	ORG	BIOS		;origin of BIOS
;
;	jump vector for individual subroutines
;
	JMP	BOOT		;cold boot
WBE	JMP	WBOOT		;warm start
	JMP	CONST		;console status
	JMP	CONIN		;console character in
	JMP	CONOUT		;console character out
	JMP	LIST		;list character out
	JMP	PUNCH		;punch character out
	JMP	READER		;reader character in
	JMP	HOME		;move disk head to home position
	JMP	SELDSK		;select disk drive
	JMP	SETTRK		;set track number
	JMP	SETSEC		;set sector number
	JMP	SETDMA		;set dma address
	JMP	READ		;read disk sector
	JMP	WRITE		;write disk sector
	JMP	LISTST		;list status
	JMP	SECTRAN		;sector translate
;
;	data tables
;
SIGNON	DB	MSIZE / 10 + '0',MSIZE MOD 10 + '0'
	DB	'K CP/M 2.2 VERS B01+HD',13,10,0
BOOTERR	DB	13,10,'BOOT ERROR',13,10,0
;
;	disk parameter header for disk 0
DPBASE	DW	TRANS,0000H
	DW	0000H,0000H
	DW	DIRBF,DPBLK
	DW	CHK00,ALL00
;	disk parameter header for disk 1
	DW	TRANS,0000H
	DW	0000H,0000H
	DW	DIRBF,DPBLK
	DW	CHK01,ALL01
;	disk parameter header for disk 2
	DW	TRANS,0000H
	DW	0000H,0000H
	DW	DIRBF,DPBLK
	DW	CHK02,ALL02
;	disk parameter header for disk 3
	DW	TRANS,0000H
	DW	0000H,0000H
	DW	DIRBF,DPBLK
	DW	CHK03,ALL03
;
;	disk parameter header for the hard disk, drive I:
;	no translate table, and no check vector because the media is fixed
DPHHD	DW	0000H,0000H
	DW	0000H,0000H
	DW	DIRBF,DPBHD
	DW	0000H,ALLHD
;
;	sector translate table for IBM 8" SD disks
TRANS	DB	1,7,13,19	;sectors 1,2,3,4
	DB	25,5,11,17	;sectors 5,6,7,8
	DB	23,3,9,15	;sectors 9,10,11,12
	DB	21,2,8,14	;sectors 13,14,15,16
	DB	20,26,6,12	;sectors 17,18,19,20
	DB	18,24,4,10	;sectors 21,22,23,24
	DB	16,22		;sectors 25,26
;
;	disk parameter block for IBM 8" SD disks
DPBLK	DW	26		;sectors per track
	DB	3		;block shift factor
	DB	7		;block mask
	DB	0		;extent mask
	DW	242		;disk size-1
	DW	63		;directory max
	DB	192		;alloc 0
	DB	0		;alloc 1
	DW	16		;check size
	DW	2		;track offset
;
;	disk parameter block for the 4 MB hard disk
;	2048 byte blocks, 2048 of them, 512 directory entries
DPBHD	DW	128		;sectors per track
	DB	4		;block shift factor
	DB	15		;block mask
	DB	0		;extent mask
	DW	2047		;disk size-1
	DW	511		;directory max
	DB	255		;alloc 0, 8 blocks of directory
	DB	0		;alloc 1
	DW	0		;check size, 0 for fixed media
	DW	0		;track offset
;
;	print a message to the console
;	pointer to string in hl
;
PRTMSG	MOV	A,M		;get next message byte
	ORA	A		;is it zero?
	RZ			;yes, done
	MOV	C,A		;no, print character on console
	CALL	CONOUT
	INX	H		;and do next
	JMP	PRTMSG
;
;	cold start
;
BOOT	LXI	SP,80H		;use space below buffer for stack
	LXI	H,SIGNON	;print signon
	CALL	PRTMSG
	XRA	A		;zero in the accumulator
	STA	CDISK		;select disk drive 0
	STA	DSKNO
	MVI	A,INITIO	;setup IOBYTE
	STA	IOBYTE
	MVI	A,10H		;setup FDC command
	OUT	FDC
	MVI	A,FDCCMD AND 0FFH
	OUT	FDC
	MVI	A,FDCCMD SHR 8
	OUT	FDC
	STC			;flag for cold start
	CMC
	JMP	GOCPM		;initialize and go to CP/M
;
;	warm start
;
WBOOT	LXI	SP,80H		;use space below buffer for stack
	MVI	C,0		;select disk 0
	CALL	SELDSK
	CALL	HOME		;go to track 0
	MVI	B,NSECTS	;B counts # of sectors to load
	MVI	C,0		;C has the current track #
	MVI	D,2		;D has the next sector to load
	LXI	H,CCP		;base of CP/M
LOAD1	PUSH	B		;save sector count and current track
	PUSH	D		;save next sector to read
	PUSH	H		;save DMA address
	MOV	C,D		;get sector address to C
	CALL	SETSEC		;set sector address
	POP	B		;recall DMA address to BC
	PUSH	B		;and replace on stack for later recall
	CALL	SETDMA		;set DMA address from BC
	CALL	READ		;read sector
	ORA	A		;any errors?
	JZ	LOAD2		;no, continue
	LXI	H,BOOTERR	;otherwise print message
	CALL	PRTMSG
	HLT			;and halt the machine
LOAD2	POP	H		;recall DMA address
	LXI	D,128		;DMA = DMA + 128
	DAD	D		;next DMA address now in HL
	POP	D		;recall sector address
	POP	B		;recall # of sectors remaining
	DCR	B		;sectors = sectors - 1
	STC			;flag for warm start
	JZ	GOCPM		;transfer to CP/M if all loaded
	INR	D		;next sector
	MOV	A,D		;sector = 27 ?
	CPI	27
	JC	LOAD1		;no, continue
	MVI	D,1		;else begin with sector 1 on next track
	INR	C
	CALL	SETTRK
	JMP	LOAD1		;for another sector
GOCPM	MVI	A,0C3H		;C3 is a JMP instruction
	STA	0		;for jmp to wboot
	LXI	H,WBE		;WBOOT entry point
	SHLD	1		;set address for JMP at 0
	STA	5		;for JMP to BDOS
	LXI	H,BDOS		;BDOS entry point
	SHLD	6		;set address for JMP at 5
	LXI	B,80H		;default dma address is 80H
	CALL	SETDMA
	LDA	CDISK		;get current disk number
	MOV	C,A		;send to the CCP
	JC	CCP+3		;go to CCP warm start
	JMP	CCP		;go to CCP cold start
;
;	jump to one of the four addresses after the call, picked by the
;	two IOBYTE bits that the shift count byte in front of them selects
;
DISPATCH XTHL			;save callers HL, get table address
	MOV	D,M		;get shift count into D
	INX	H		;point to table
	LDA	IOBYTE
DSHIFT	RLC			;shift the field into bits 2-1
	DCR	D
	JNZ	DSHIFT
	ANI	06H
	MOV	E,A		;D is already clear
	DAD	D
	MOV	A,M		;get table word into HL
	INX	H
	MOV	H,M
	MOV	L,A
	XTHL			;put routine address, get callers HL
	RET			;go to routine
;
;	console status, return 0FFH if character ready, 00H if not
;
CONST	CALL	DISPATCH
	DB	1		;bits 1-0
	DW	TTYIS		;TTY:
	DW	TTYIS		;CRT:
	DW	TTY2IS		;BAT:
	DW	MODIS		;UC1:
;
;	console input character into register A
;
CONIN	CALL	DISPATCH
	DB	1		;bits 1-0
	DW	TTYIN		;TTY:
	DW	TTYIN		;CRT:
	DW	TTY2IN		;BAT:
	DW	MODIN		;UC1:
;
;	console output character from register C
;
CONOUT	CALL	DISPATCH
	DB	1		;bits 1-0
	DW	TTYOUT		;TTY:
	DW	TTYOUT		;CRT:
	DW	LPTOUT		;BAT:
	DW	MODOUT		;UC1:
;
;	printer status, return 0FFH if ready, 00H if not
;
LISTST	CALL	DISPATCH
	DB	3		;bits 7-6
	DW	TTYOS		;TTY:
	DW	TTYOS		;CRT:
	DW	NOTRDY		;LPT:
	DW	TTY2OS		;UL1:
;
;	list character from register C
;
LIST	CALL	DISPATCH
	DB	3		;bits 7-6
	DW	TTYOUT		;TTY:
	DW	TTYOUT		;CRT:
	DW	LPTOUT		;LPT:
	DW	TTY2OUT		;UL1:
;
;	punch character from register C
;
PUNCH	CALL	DISPATCH
	DB	5		;bits 5-4
	DW	TTYOUT		;TTY:
	DW	TTY2OUT		;PTP:
	DW	MODOUT		;UP1:
	DW	TTYOUT		;UP2:
;
;	read character into register A from reader
;
READER	CALL	DISPATCH
	DB	7		;bits 3-2
	DW	TTYIN		;TTY:
	DW	TTY2IN		;PTR:
	DW	MODIN		;UR1:
	DW	TTYIN		;UR2:
;
;	console, the screen and keyboard (status bits are active low)
;
TTYIS	IN	CONSTA
	RRC			;test bit 0
	JC	NOTRDY
DEVRDY	MVI	A,0FFH
	RET
NOTRDY	XRA	A
	RET
;
TTYOS	IN	CONSTA
	RLC			;test bit 7
	JC	NOTRDY
	JMP	DEVRDY
;
TTYIN	IN	CONSTA
	RRC			;test bit 0
	JC	TTYIN		;not ready
	IN	CONDAT
	RET
;
TTYOUT	IN	CONSTA
	RLC			;test bit 7
	JC	TTYOUT		;wait until transmitter ready
	MOV	A,C
	OUT	CONDAT
	RET
;
;	there is no printer, so output to it is thrown away
;
LPTOUT	RET
;
;	SIO-2 port A, the USB-C serial line (8251 status bits)
;
TTY2IS	IN	TTY2S
	ANI	02H		;receiver ready?
	RZ
	JMP	DEVRDY
;
TTY2OS	IN	TTY2S
	ANI	01H		;transmitter ready?
	RZ
	JMP	DEVRDY
;
TTY2IN	IN	TTY2S
	ANI	02H
	JZ	TTY2IN		;wait until character available
	IN	TTY2
	RET
;
TTY2OUT	IN	TTY2S
	RRC
	JNC	TTY2OUT		;wait until transmitter ready
	MOV	A,C
	OUT	TTY2
	RET
;
;	SIO-2 port B, the WiFi modem (8251 status bits)
;
MODIS	IN	MODEMS
	ANI	02H		;receiver ready?
	RZ
	JMP	DEVRDY
;
MODIN	IN	MODEMS
	ANI	02H
	JZ	MODIN		;wait until character available
	IN	MODEM
	RET
;
MODOUT	IN	MODEMS
	RRC
	JNC	MODOUT		;wait until transmitter ready
	MOV	A,C
	OUT	MODEM
	RET
;
;	move to track 0 position on current disk
;
HOME	MVI	C,0		;select track 0
	JMP	SETTRK
;
;	select disk given by register C
;
SELDSK	LXI	H,0		;error return code
	MOV	A,C		;get disk # to accumulator
	CPI	4		;disk drive < 4 ?
	JC	SEL1
	CPI	8		;the hard disk is drive I:
	RNZ			;anything else does not exist
	STA	DSKNO		;save disk #
	LXI	H,DPHHD		;HL=.DPHHD
	RET
SEL1	STA	DSKNO		;save disk #
	MOV	L,C		;HL = disk #
	DAD	H		;*2
	DAD	H		;*4
	DAD	H		;*8
	DAD	H		;*16 (size of each header)
	LXI	D,DPBASE
	DAD	D		;HL=.DPBASE(DISKNO*16)
	RET
;
;	set track given by register C
;
SETTRK	MOV	A,C		;get to accumulator
	STA	FDCCMD+DDTRK	;set in FDC command
        RET
;
;	set sector given by register C
;
SETSEC	MOV	A,C		;get to accumulator
	STA     FDCCMD+DDSEC	;set in FDC command
        RET
;
;	set DMA address given by registers B and C
;
SETDMA	MOV	A,C		;low order address
	STA	FDCCMD+DDLDMA	;set in FDC command
	MOV	A,B		;high order address
	STA	FDCCMD+DDHDMA	;set in FDC command
        RET
;
;	perform read operation
;
READ	LDA     DSKNO		;get disk #
	ORI	20H		;mask in read command
        JMP     DOIO            ;do I/O operation
;
;	perform write operation
;
WRITE	LDA	DSKNO		;get disk #
	ORI	40H		;mask in write command
	JMP	DOIO		;do I/O operation
;
;	perform read/write I/O
;
DOIO	OUT	FDC		;ask FDC to execute the command
	IN	FDC		;get status from FDC
        RET
;
;	translate the sector given by BC using
;	the translation table given by DE
;
SECTRAN	MOV	A,D		;no translate table ?
	ORA	E
	JNZ	SECT1
	MOV	H,B		;HL=sector
	MOV	L,C
	INX	H		;the controller numbers sectors from 1
	RET
SECT1	XCHG			;HL=.TRANS
	DAD	B               ;HL=.TRANS(SECTOR)
	XCHG
	LDAX	D
	MOV	L,A		;L=TRANS(SECTOR)
	MVI	H,0		;HL=TRANS(SECTOR)
	RET			;with value in HL
;
;	The remainder of the CBIOS is reserved uninitialized
;	data area, and does not need to be part of the system
;	memory image. The space must be available, however,
;	between "BEGDAT" and "ENDDAT".
;
BEGDAT	EQU	$		;begin of data area
;
DSKNO	DS	1		;selected disk
;
DIRBF	DS	128		;scratch directory area
ALL00	DS	31		;allocation vector 0
ALL01	DS	31		;allocation vector 1
ALL02	DS	31		;allocation vector 2
ALL03	DS	31		;allocation vector 3
CHK00	DS	16		;check vector 0
CHK01	DS	16		;check vector 1
CHK02	DS	16		;check vector 2
CHK03	DS	16		;check vector 3
ALLHD	DS	256		;allocation vector for the hard disk
;
ENDDAT	EQU	$		;end of data area
DATSIZ	EQU	$-BEGDAT	;size of data area
;
	END			;of CBIOS
