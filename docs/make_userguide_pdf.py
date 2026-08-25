#!/usr/bin/env python3
#++
#	FACILITY: A yet another gateway for MODBUS (TCP to RTU)
#
#	DESCRIPTION: Generator of the net2serial User Guide PDFs (English + Russian) in the
#		classic DEC/VSI documentation style: the title page with the document number
#		block, the legal page, a dotted-leaders table of contents, the numbered Preface,
#		chapters/sections, ruled tables, monospaced examples and vector diagrams.
#		The front matter is numbered by the roman numerals, the body - by the arabic
#		ones, exactly as the VSI manuals do.
#
#	USAGE:
#		$ python3 make_userguide_pdf.py <EN|RU> <output.pdf>
#
#	AUTHOR: StarLet Squad and Ruslan R. Laishev (AKA: BadAss sysman)
#
#	CREATION DATE: 25-AUG-2026
#--
import sys

from reportlab.lib.pagesizes import A4
from reportlab.lib.units import mm
from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.enums import TA_LEFT, TA_CENTER
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (BaseDocTemplate, PageTemplate, Frame, Paragraph, Spacer,
	PageBreak, Table, TableStyle, Preformatted, NextPageTemplate, KeepTogether)
from reportlab.platypus.tableofcontents import TableOfContents
from reportlab.graphics.shapes import Drawing, Rect, Line, String, Polygon, Group

# ----------------------------------------------------------------------------------------------
#	Fonts: the DejaVu family covers both the Latin and the Cyrillic scripts
# ----------------------------------------------------------------------------------------------
FDIR = '/usr/share/fonts/truetype/dejavu/'
pdfmetrics.registerFont(TTFont('Serif',   FDIR + 'DejaVuSerif.ttf'))
pdfmetrics.registerFont(TTFont('SerifB',  FDIR + 'DejaVuSerif-Bold.ttf'))
pdfmetrics.registerFont(TTFont('SerifI',  FDIR + 'DejaVuSerif-Italic.ttf'))
pdfmetrics.registerFont(TTFont('Sans',    FDIR + 'DejaVuSans.ttf'))
pdfmetrics.registerFont(TTFont('SansB',   FDIR + 'DejaVuSans-Bold.ttf'))
pdfmetrics.registerFont(TTFont('Mono',    FDIR + 'DejaVuSansMono.ttf'))
pdfmetrics.registerFont(TTFont('MonoB',   FDIR + 'DejaVuSansMono-Bold.ttf'))

PAGE_W, PAGE_H = A4
LM, RM, TM, BM = 25*mm, 22*mm, 22*mm, 22*mm

VERSION   = 'X.00-05'
PUBDATE_EN = 'August 2026'
PUBDATE_RU = 'Август 2026'
ORG       = 'NaPi World & StarLet Squad collaboration'

# ----------------------------------------------------------------------------------------------
#	Styles (the VSI look: serif body, bold sans-ish headings, mono examples)
# ----------------------------------------------------------------------------------------------
S = {}
S['body']   = ParagraphStyle('body',   fontName='Serif',  fontSize=10, leading=13.5,
	spaceAfter=6, alignment=TA_LEFT)
S['bodyi']  = ParagraphStyle('bodyi',  parent=S['body'], fontName='SerifI')
S['chap']   = ParagraphStyle('chap',   fontName='SerifB', fontSize=20, leading=24,
	spaceBefore=0, spaceAfter=18)
S['sec']    = ParagraphStyle('sec',    fontName='SerifB', fontSize=13.5, leading=17,
	spaceBefore=14, spaceAfter=7)
S['subsec'] = ParagraphStyle('subsec', fontName='SerifB', fontSize=11.5, leading=15,
	spaceBefore=10, spaceAfter=5)
S['code']   = ParagraphStyle('code',   fontName='Mono',   fontSize=8.4, leading=10.6,
	leftIndent=6*mm, spaceBefore=4, spaceAfter=8)
S['tcell']  = ParagraphStyle('tcell',  fontName='Serif',  fontSize=9,  leading=11.6)
S['tcellm'] = ParagraphStyle('tcellm', fontName='Mono',   fontSize=8.4, leading=10.8)
S['thead']  = ParagraphStyle('thead',  fontName='SerifB', fontSize=9,  leading=11.6)
S['caption']= ParagraphStyle('caption',fontName='SerifB', fontSize=9.5, leading=12,
	spaceBefore=4, spaceAfter=10, alignment=TA_CENTER)
S['title1'] = ParagraphStyle('title1', fontName='SerifB', fontSize=24, leading=30)
S['title2'] = ParagraphStyle('title2', fontName='Serif',  fontSize=17, leading=22)
S['tmeta']  = ParagraphStyle('tmeta',  fontName='Serif',  fontSize=10.5, leading=16)
S['legal']  = ParagraphStyle('legal',  fontName='Serif',  fontSize=8.5, leading=11,
	spaceAfter=6)
S['toc0']   = ParagraphStyle('toc0', fontName='SerifB', fontSize=10.5, leading=15, leftIndent=0)
S['toc1']   = ParagraphStyle('toc1', fontName='Serif',  fontSize=9.5, leading=13, leftIndent=8*mm)

TBL_STYLE = TableStyle([
	('FONT',          (0,0), (-1,-1), 'Serif', 9),
	('LINEABOVE',     (0,0), (-1,0),  1.0, colors.black),
	('LINEBELOW',     (0,0), (-1,0),  0.6, colors.black),
	('LINEBELOW',     (0,-1),(-1,-1), 1.0, colors.black),
	('VALIGN',        (0,0), (-1,-1), 'TOP'),
	('TOPPADDING',    (0,0), (-1,-1), 3),
	('BOTTOMPADDING', (0,0), (-1,-1), 3),
	('LEFTPADDING',   (0,0), (-1,-1), 4),
	('RIGHTPADDING',  (0,0), (-1,-1), 4),
])

def tbl (a_head, a_rows, a_widths, a_mono_cols=()):
	l_data = [[Paragraph(h, S['thead']) for h in a_head]]
	for l_r in a_rows:
		l_data.append([Paragraph(c, S['tcellm'] if i in a_mono_cols else S['tcell'])
			for i, c in enumerate(l_r)])
	l_t = Table(l_data, colWidths=a_widths, repeatRows=1)
	l_t.setStyle(TBL_STYLE)
	return l_t

# ----------------------------------------------------------------------------------------------
#	Vector diagrams
# ----------------------------------------------------------------------------------------------
def s_box (a_g, a_x, a_y, a_w, a_h, a_lines, a_fs=8.5, a_fill=colors.whitesmoke, a_font='Sans'):
	a_g.add(Rect(a_x, a_y, a_w, a_h, fillColor=a_fill, strokeColor=colors.black, strokeWidth=1))
	l_n = len(a_lines)
	for i, l_t in enumerate(a_lines):
		l_ty = a_y + a_h/2 + (l_n - 1 - 2*i) * (a_fs + 2)/2 - a_fs*0.35
		a_g.add(String(a_x + a_w/2, l_ty, l_t, fontName=a_font, fontSize=a_fs,
			textAnchor='middle'))

def s_arrow (a_g, a_x1, a_y1, a_x2, a_y2, a_both=False):
	a_g.add(Line(a_x1, a_y1, a_x2, a_y2, strokeWidth=1.2))
	import math
	l_ang = math.atan2(a_y2 - a_y1, a_x2 - a_x1)
	for (l_tx, l_ty, l_a) in ([(a_x2, a_y2, l_ang)] + ([(a_x1, a_y1, l_ang + math.pi)] if a_both else [])):
		l_p1 = (l_tx - 7*math.cos(l_a - 0.42), l_ty - 7*math.sin(l_a - 0.42))
		l_p2 = (l_tx - 7*math.cos(l_a + 0.42), l_ty - 7*math.sin(l_a + 0.42))
		a_g.add(Polygon([l_tx, l_ty, l_p1[0], l_p1[1], l_p2[0], l_p2[1]],
			fillColor=colors.black, strokeColor=colors.black))

def dia_architecture (a_L):
	"""The data path: a TCP client -> the gateway -> the serial device."""
	l_d = Drawing(460, 150)
	g = Group(); l_d.add(g)

	s_box(g, 6, 62, 108, 44, [a_L['dia_client'], a_L['dia_clienttool']], 8)
	s_box(g, 168, 56, 126, 56, ['net2serial', a_L['dia_oneowner']], 9, colors.Color(0.88, 0.92, 1))
	s_box(g, 350, 62, 104, 44, [a_L['dia_device'], a_L['dia_devicekind']], 8)

	s_arrow(g, 114, 84, 168, 84, a_both=True)
	s_arrow(g, 294, 84, 350, 84, a_both=True)

	g.add(String(141, 116, 'TCP/IP', fontName='Sans', fontSize=7.5, textAnchor='middle'))
	g.add(String(322, 116, 'RS-232 / RS-485', fontName='Sans', fontSize=7.5, textAnchor='middle'))
	g.add(String(141, 66, a_L['dia_octets'], fontName='SerifI', fontSize=7.5, textAnchor='middle'))
	g.add(String(322, 66, a_L['dia_octets'], fontName='SerifI', fontSize=7.5, textAnchor='middle'))

	# the rejected second client
	s_box(g, 6, 8, 108, 30, [a_L['dia_client2']], 8, colors.Color(1, 0.92, 0.88))
	s_arrow(g, 114, 23, 168, 56)
	g.add(String(258, 26, a_L['dia_rejected'], fontName='Sans', fontSize=7.5, textAnchor='middle'))
	return l_d


def dia_rings (a_L):
	"""The two lanes of the data flow and the poll() events derived from the ring state."""
	l_d = Drawing(460, 190)
	g = Group(); l_d.add(g)

	# Lane 1: the network -> the serial line
	s_box(g,   6, 138, 96, 34, [a_L['dia_socket']], 8)
	s_box(g, 158, 138, 144, 34, [a_L['dia_ringnet']], 8, colors.Color(0.88, 0.92, 1))
	s_box(g, 358, 138, 96, 34, [a_L['dia_port']], 8)
	s_arrow(g, 102, 155, 158, 155)
	s_arrow(g, 302, 155, 358, 155)
	g.add(String(130, 176, 'POLLIN', fontName='Mono', fontSize=7, textAnchor='middle'))
	g.add(String(330, 176, 'POLLOUT', fontName='Mono', fontSize=7, textAnchor='middle'))

	# Lane 2: the serial line -> the network
	s_box(g,   6,  62, 96, 34, [a_L['dia_port']], 8)
	s_box(g, 158,  62, 144, 34, [a_L['dia_ringtty']], 8, colors.Color(0.88, 1, 0.9))
	s_box(g, 358,  62, 96, 34, [a_L['dia_socket']], 8)
	s_arrow(g, 102, 79, 158, 79)
	s_arrow(g, 302, 79, 358, 79)
	g.add(String(130, 100, 'POLLIN', fontName='Mono', fontSize=7, textAnchor='middle'))
	g.add(String(330, 100, 'POLLOUT', fontName='Mono', fontSize=7, textAnchor='middle'))

	# The two rules which drive the whole loop
	g.add(String(230, 30, a_L['dia_rule1'], fontName='SerifI', fontSize=8, textAnchor='middle'))
	g.add(String(230, 14, a_L['dia_rule2'], fontName='SerifI', fontSize=8, textAnchor='middle'))
	return l_d


def dia_ownership (a_L):
	"""The lifecycle of the port ownership."""
	l_d = Drawing(460, 160)
	g = Group(); l_d.add(g)

	s_box(g, 8,   96, 96, 40, [a_L['dia_free']], 8, colors.Color(0.88, 1, 0.9))
	s_box(g, 176, 96, 108, 40, [a_L['dia_owned']], 8, colors.Color(0.88, 0.92, 1))
	s_box(g, 352, 96, 100, 40, [a_L['dia_freed']], 8, colors.Color(0.88, 1, 0.9))
	s_box(g, 176, 18, 108, 38, [a_L['dia_busy']], 8, colors.Color(1, 0.92, 0.88))

	s_arrow(g, 104, 116, 176, 116)
	s_arrow(g, 284, 116, 352, 116)
	s_arrow(g, 230, 96, 230, 56)

	g.add(String(140, 122, a_L['dia_accept'], fontName='Sans', fontSize=7, textAnchor='middle'))
	g.add(String(318, 122, a_L['dia_disc'], fontName='Sans', fontSize=7, textAnchor='middle'))
	g.add(String(300, 74, a_L['dia_second'], fontName='Sans', fontSize=7, textAnchor='middle'))
	return l_d


def dia_msg_anatomy (a_L):
	"""The anatomy of a log message line."""
	l_d = Drawing(460, 110)
	g = Group(); l_d.add(g)
	l_msg = '%N2S-E-DEVOPNERR, Cannot open the device </dev/ttyUSB0>, errno: 2'
	g.add(String(20, 62, l_msg, fontName='Mono', fontSize=9))
	l_cw = 5.42
	l_x0 = 20
	for (l_off, l_len, l_tx, l_lx) in (
			(0, 4,  a_L['dia_fac'], 30),
			(4, 2,  a_L['dia_sev'], 150),
			(6, 10, a_L['dia_code'], 300)):
		l_a = l_x0 + l_off*l_cw
		l_b = l_a + l_len*l_cw
		g.add(Line(l_a, 58, l_b, 58, strokeWidth=1.4))
		g.add(Line((l_a + l_b)/2, 58, (l_a + l_b)/2, 44, strokeWidth=0.9))
		g.add(Line((l_a + l_b)/2, 44, l_lx, 36, strokeWidth=0.9))
		g.add(String(l_lx, 26, l_tx, fontName='Sans', fontSize=8, textAnchor='middle'))
	g.add(String(20, 88, a_L['dia_msgline'], fontName='SerifI', fontSize=8.5))
	return l_d


# ----------------------------------------------------------------------------------------------
#	The document template: VSI-style running heads, roman front matter / arabic body
# ----------------------------------------------------------------------------------------------
def s_roman (a_n):
	l_map = [(10, 'x'), (9, 'ix'), (5, 'v'), (4, 'iv'), (1, 'i')]
	l_out = ''
	while a_n > 0:
		for l_v, l_s in l_map:
			if a_n >= l_v:
				l_out += l_s; a_n -= l_v; break
	return l_out

class GuideDoc (BaseDocTemplate):
	def __init__ (self, a_fn, a_L, **kw):
		super().__init__(a_fn, pagesize=A4, leftMargin=LM, rightMargin=RM,
			topMargin=TM, bottomMargin=BM, **kw)
		self.m_L = a_L
		self.m_bodypage = 10**9			# The first arabic (body) page, found at pass 1
		l_frame = Frame(LM, BM, PAGE_W - LM - RM, PAGE_H - TM - BM, id='main')
		self.addPageTemplates([
			PageTemplate(id='Title', frames=[l_frame], onPage=self.s_pg_title),
			PageTemplate(id='Front', frames=[l_frame], onPage=self.s_pg_front),
			PageTemplate(id='Body',  frames=[l_frame], onPage=self.s_pg_body),
		])

	def s_head (self, a_cv):
		a_cv.setFont('Serif', 8.5)
		a_cv.drawCentredString(PAGE_W/2, PAGE_H - 14*mm, self.m_L['runhead'])
		a_cv.setLineWidth(0.5)
		a_cv.line(LM, PAGE_H - 16*mm, PAGE_W - RM, PAGE_H - 16*mm)

	def s_pg_title (self, a_cv, a_doc):
		pass

	def s_pg_front (self, a_cv, a_doc):
		self.s_head(a_cv)
		a_cv.setFont('Serif', 9)
		a_cv.drawCentredString(PAGE_W/2, 12*mm, s_roman(a_doc.page))

	def s_pg_body (self, a_cv, a_doc):
		if a_doc.page < self.m_bodypage:
			self.m_bodypage = a_doc.page
		self.s_head(a_cv)
		a_cv.setFont('Serif', 9)
		a_cv.drawCentredString(PAGE_W/2, 12*mm, str(a_doc.page - self.m_bodypage + 1))

	def afterFlowable (self, a_fl):
		if isinstance(a_fl, Paragraph) and a_fl.style.name in ('chap', 'sec'):
			l_lvl = 0 if a_fl.style.name == 'chap' else 1
			l_txt = a_fl.getPlainText()

			if l_txt == self.m_L['toc_h']:			# The Contents itself is not listed
				return

			l_key = 'k' + str(abs(hash(l_txt)) % 10**8)
			self.canv.bookmarkPage(l_key)
			self.notify('TOCEntry', (l_lvl, l_txt, self.page, l_key))

# ----------------------------------------------------------------------------------------------
#	The content, parameterized by the language pack
# ----------------------------------------------------------------------------------------------
def build (a_L, a_out):
	l_doc = GuideDoc(a_out, a_L, title=a_L['doctitle'], author=ORG)
	l_st = []

	# --- The title page (the VSI canon) ---
	l_st.append(Spacer(1, 30*mm))
	l_st.append(Paragraph('mbusgw-t2r', S['title1']))
	l_st.append(Paragraph(a_L['doctitle'], S['title2']))
	l_st.append(Spacer(1, 18*mm))
	for l_k, l_v in a_L['titleblock']:
		l_st.append(Paragraph('<b>%s</b> %s' % (l_k, l_v), S['tmeta']))
	l_st.append(Spacer(1, 42*mm))
	l_st.append(Paragraph(ORG, S['tmeta']))
	l_st.append(NextPageTemplate('Front'))
	l_st.append(PageBreak())

	# --- The legal page ---
	l_st.append(Paragraph(a_L['runhead'], S['legal']))
	l_st.append(Spacer(1, 8*mm))
	l_st.append(Paragraph(a_L['copyright'], S['legal']))
	l_st.append(Paragraph('<b>%s</b>' % a_L['legal_h'], S['legal']))
	for l_p in a_L['legal']:
		l_st.append(Paragraph(l_p, S['legal']))
	l_st.append(PageBreak())

	# --- The table of contents ---
	l_toc = TableOfContents()
	l_toc.levelStyles = [S['toc0'], S['toc1']]
	l_toc.dotsMinLevel = 0

	#
	# The displayed page number follows the VSI canon: the roman numerals for the front
	# matter, the arabic ones (restarted from 1) for the body. The body start page becomes
	# known after the first pass of multiBuild() and is applied by the subsequent passes.
	#
	def s_pgfmt (a_p):
		if l_doc.m_bodypage >= 10**9:
			return	str(a_p)
		if a_p >= l_doc.m_bodypage:
			return	str(a_p - l_doc.m_bodypage + 1)
		return	s_roman(a_p)

	l_toc.formatter = s_pgfmt
	l_st.append(Paragraph(a_L['toc_h'], S['chap']))
	l_st.append(l_toc)
	l_st.append(PageBreak())

	# --- The Preface (numbered subsections, per the VSI canon) ---
	l_st.append(Paragraph(a_L['preface_h'], S['chap']))
	for l_n, (l_h, l_paras) in enumerate(a_L['preface'], 1):
		l_st.append(Paragraph('%d. %s' % (l_n, l_h), S['sec']))
		for l_p in l_paras:
			l_st.append(Paragraph(l_p, S['body']))
	l_st.append(Paragraph('%d. %s' % (len(a_L['preface']) + 1, a_L['conv_h']), S['sec']))
	l_st.append(tbl([a_L['conv_c1'], a_L['conv_c2']], a_L['conventions'],
		[38*mm, 125*mm], a_mono_cols=(0,)))
	l_st.append(NextPageTemplate('Body'))
	l_st.append(PageBreak())

	# --- The chapters ---
	l_fig = [0]
	def fig (a_dia, a_cap):
		l_fig[0] += 1
		return KeepTogether([Spacer(1, 3*mm), a_dia,
			Paragraph(a_L['fig_w'] % l_fig[0] + a_cap, S['caption'])])

	for l_ch in a_L['chapters'](fig):
		for l_el in l_ch:
			l_st.append(l_el)
		l_st.append(PageBreak())
	del l_st[-1]

	l_doc.multiBuild(l_st)

# ----------------------------------------------------------------------------------------------
#	Helper constructors used by the language packs
# ----------------------------------------------------------------------------------------------
def P  (a_t): return Paragraph(a_t, S['body'])
def PI (a_t): return Paragraph(a_t, S['bodyi'])
def C  (a_t): return Preformatted(a_t, S['code'])
def H1 (a_t): return Paragraph(a_t, S['chap'])
def H2 (a_t): return Paragraph(a_t, S['sec'])
def H3 (a_t): return Paragraph(a_t, S['subsec'])

# ----------------------------------------------------------------------------------------------
#	The ENGLISH language pack
# ----------------------------------------------------------------------------------------------
def s_pack_en ():
	L = {}
	L['doctitle'] = 'User Guide'
	L['runhead']  = 'net2serial User Guide'
	L['titleblock'] = [
		('Document Number:', 'DO-N2SUG-EN-01A'),
		('Publication Date:', PUBDATE_EN),
		('Revision Update Information:', 'This is a new manual.'),
		('Operating System:', 'Linux (glibc), kernel 4.x or later'),
		('Software Version:', 'net2serial ' + VERSION),
	]
	L['copyright'] = 'Copyright © 2026 %s' % ORG
	L['legal_h'] = 'Legal Notice'
	L['legal'] = [
		'The information contained herein is subject to change without notice. '
		'%s shall not be liable for technical or editorial errors or omissions contained herein.' % ORG,
		'The software described in this manual is distributed in the hope that it will be useful, '
		'but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS '
		'FOR A PARTICULAR PURPOSE.',
		'Linux is a registered trademark of Linus Torvalds. UNIX is a registered trademark of '
		'The Open Group. All other trademarks are the property of their respective owners.',
	]
	L['toc_h'] = 'Contents'
	L['preface_h'] = 'Preface'
	L['preface'] = [
		('About This Manual', [
			'This manual describes the configuration, the operation and the troubleshooting of '
			'<b>net2serial</b> --- a gateway which passes a raw octets stream between a TCP '
			'connection and a local serial port, in both directions and without interpreting it.']),
		('Intended Audience', [
			'This manual is written for a reader with no prior serial line or Linux administration '
			'experience: every step is spelled out and nothing is assumed. An experienced engineer '
			'may go straight to Chapter 3 (the settings file) and Chapter 6 (the troubleshooting).']),
		('Document Structure', [
			'Chapter 1 explains what the gateway does and why a port is given to a single client. '
			'Chapter 2 is the pre-flight checklist. Chapter 3 describes the settings file key by '
			'key. Chapter 4 covers the start, the command line options and the first test. '
			'Chapter 5 teaches how to read the log. Chapter 6 is the troubleshooting reference: '
			'the symptom, the cause, the action, and the reference of all the message codes.']),
		('Related Documents', [
			'<i>README.md</i> --- the installation sequence (the StarLet package, the build, '
			'<font face="Mono">make install</font>). The manual pages of <font face="Mono">termios</font>(3) '
			'and <font face="Mono">stty</font>(1) --- the serial line parameters of the operating system.']),
	]
	L['conv_h'] = 'Conventions'
	L['conv_c1'] = 'Convention'; L['conv_c2'] = 'Meaning'
	L['conventions'] = [
		('Monospace type', 'Code examples, file names, commands and interactive screen displays.'),
		('%N2S-E-CODE',    'A message code as it appears in the gateway log; see Section 6.2.'),
		('<...>',          'A placeholder to be replaced by an actual value, e.g. a device name.'),
		('$',              'The shell prompt of an unprivileged user; do not type it.'),
		('Ctrl/C',         'Hold down the key labeled Ctrl while you press the key C.'),
	]
	L.update({
		'dia_client': 'TCP client', 'dia_clienttool': 'telnet, nc, a terminal',
		'dia_oneowner': '(one owner at a time)', 'dia_device': 'Your device',
		'dia_devicekind': 'console, PLC, UPS ...', 'dia_octets': 'a raw octets stream',
		'dia_client2': 'A second TCP client', 'dia_rejected': 'is rejected: DEVBUSY',
		'dia_socket': 'TCP socket', 'dia_port': 'Serial port',
		'dia_ringnet': 'Network ring buffer', 'dia_ringtty': 'Serial ring buffer',
		'dia_ringtitle': 'The poll() events are derived from the state of the rings',
		'dia_rule1': 'A free space in a ring  ->  we ask for POLLIN on its source',
		'dia_rule2': 'A data in a ring  ->  we ask for POLLOUT on its destination',
		'dia_free': 'The port is free', 'dia_owned': 'Owned by the session #N',
		'dia_freed': 'The port is free again', 'dia_busy': 'A second client: DEVBUSY',
		'dia_accept': 'accept + acquire', 'dia_disc': 'disconnect + release',
		'dia_second': 'refused, the owner is not disturbed',
		'dia_owntitle': 'The lifecycle of the port ownership',
		'dia_fac': 'the facility', 'dia_sev': 'the severity: S I W E F',
		'dia_code': 'the message code (Section 6.2)',
		'dia_msgline': 'Every important event is one line with a code:',
	})
	L['fig_w'] = 'Figure %d.  '

	def chapters (fig):
		return [
		[H1('Chapter 1.  What the Gateway Does'),
		 P('You have a device with a serial console or a serial interface --- a switch, a router, '
		   'an old VAX, a PLC, a UPS. It is plugged into the serial port of the computer where this '
		   'gateway runs. And you want to reach that device over the network, from your desk. '
		   'The gateway sits between them and passes the octets through, as Figure 1 shows.'),
		 fig(dia_architecture(L), 'The data path of the gateway'),
		 P('Nothing is interpreted: what you type goes to the device as is, what the device prints '
		   'comes back as is. This is why it works with anything --- a login prompt, a bootloader, '
		   'a binary protocol.'),
		 H2('1.1.  One Client at a Time per Port'),
		 P('An octets stream has no framing. If two people typed into one console at once, their '
		   'characters would interleave and neither would get a readable answer. So a serial port '
		   'is given to a single session: a second client is refused with a clear message and the '
		   'first one keeps working undisturbed. As soon as the first client disconnects, the port '
		   'is free for the next one --- see Figure 2.'),
		 fig(dia_ownership(L), 'The lifecycle of the port ownership')],
		[H1('Chapter 2.  Before You Start'),
		 P('Walk this checklist once --- it saves an hour of guessing later.'),
		 P('<b>1.</b> The gateway is installed (see <i>README.md</i>). After the installation you have '
		   'the program <font face="Mono">/usr/local/sbin/net2serial</font> and the settings file '
		   '<font face="Mono">/usr/local/etc/net2serial/net2serial_settings.conf</font>.'),
		 P('<b>2.</b> You know which serial port your device is connected to. On Linux it is a file like '
		   '<font face="Mono">/dev/ttyS0</font> or <font face="Mono">/dev/ttyUSB0</font>. If unsure, '
		   'plug the USB adapter out and in, then run:'),
		 C('$ dmesg | tail'),
		 P('<b>3.</b> You know the line parameters of the device: the speed, the data bits, the parity, '
		   'the stop bits and the flow control. They are in the device manual; the most common console '
		   'set is <font face="Mono">9600, 8, N, 1</font> with no flow control.'),
		 P('<b>4.</b> Your user may open the port. Check the group of the port file:'),
		 C('$ ls -l /dev/ttyUSB0\n'
		   'crw-rw---- 1 root dialout 188, 0 ... /dev/ttyUSB0'),
		 P('If the group is <font face="Mono">dialout</font>, add yourself to it and re-login '
		   '(or simply run the gateway with <font face="Mono">sudo</font>):'),
		 C('$ sudo usermod -a -G dialout $USER')],
		[H1('Chapter 3.  The Settings File'),
		 P('The settings file has two sections: <font face="Mono">serials</font> (the serial ports) '
		   'and <font face="Mono">listeners</font> (the TCP ports). A minimal working example:'),
		 C('serials = (\n'
		   '\t{\tdevice = "/dev/ttyUSB0";\n'
		   '\t\tchars  = "9600, 8, N, 1";\n'
		   '\t\tflow   = "NONE";\n'
		   '\t}\n'
		   ');\n\n'
		   'listeners = (\n'
		   '\t{\tbind   = "TCP:0.0.0.0:5000";\n'
		   '\t\ttarget = "/dev/ttyUSB0";\n'
		   '\t}\n'
		   ');'),
		 P('Read it as: <i>"open /dev/ttyUSB0 at 9600-8-N-1 with no flow control; listen for TCP '
		   'clients on every interface, port 5000, and connect them to that port"</i>.'),
		 H2('3.1.  The serials Section'),
		 tbl(['Key', 'Required', 'Meaning and the allowed values'], [
			('device', 'yes', 'The serial port file, e.g. /dev/ttyUSB0.'),
			('chars', 'yes', 'The line parameters: speed, data bits, parity, stop bits. '
				'Speed 50..4000000; data bits 5..8; parity N (none), E (even), O (odd); stop bits 1..2.'),
			('flow', 'no', 'Flow control: NONE, XON/XOFF, RTS/CTS. Default NONE.'),
			('iotmo', 'no', 'The I/O timeout, milliseconds, 1..600000. Default 3000.'),
			('rs485', 'no', '1 --- ask the kernel to drive the RS-485 direction control. Default 0.'),
			('desc', 'no', 'A free text description, for your own convenience.'),
		 ], [26*mm, 20*mm, 117*mm], a_mono_cols=(0,)),
		 P('<b>Which flow control to pick?</b> If the device manual says nothing --- NONE. A '
		   'Cisco-style console cable usually wants RTS/CTS. Old terminals and printers often want '
		   'XON/XOFF. If you paste a long text and the tail comes out garbled, the flow control is '
		   'the first thing to check.'),
		 P('If a record is wrong, the gateway skips it and says why --- with the allowed range right '
		   'in the message:'),
		 C('%NET2SER-E:  [serial #00:</dev/ttyUSB0>] --- speed 31 baud is out of range [50..4000000]'),
		 H2('3.2.  The listeners Section'),
		 tbl(['Key', 'Required', 'Meaning and the allowed values'], [
			('bind', 'yes', 'Where to listen: TCP:&lt;IP address&gt;:&lt;port&gt;. The port is 1..65535; '
				'0.0.0.0 means every interface. UDP is not supported.'),
			('target', 'yes', 'Which serial port to connect to. Must match a device from serials '
				'exactly, character by character.'),
			('connlm', 'no', 'The backlog of the listening socket, 1..128. This is NOT a count of '
				'simultaneous clients --- see Section 1.1.'),
			('iotmo', 'no', 'The network I/O timeout, milliseconds, 1..600000.'),
		 ], [26*mm, 20*mm, 117*mm], a_mono_cols=(0,)),
		 P('Ports below 1024 need the root privileges on Linux. If you do not want to run as root, '
		   'take e.g. <font face="Mono">5000</font> and point your client there.')],
		[H1('Chapter 4.  Running the Gateway'),
		 C('$ /usr/local/sbin/net2serial /settings=/usr/local/etc/net2serial/net2serial_settings.conf'),
		 tbl(['Option', 'Meaning'], [
			('/settings=&lt;file&gt;', 'The settings file (Chapter 3).'),
			('/trace', 'The verbose tracing: every chunk of octets is reported. Priceless at the first run.'),
			('/logfile=&lt;file&gt;', 'Write the log to a file instead of the screen.'),
			('/logsize=&lt;octets&gt;', 'Rotate the log file above this size.'),
		 ], [42*mm, 121*mm], a_mono_cols=(0,)),
		 P('A healthy start prints (shortened):'),
		 C('%N2S-I-REVISNF, Rev: NET2SER X.00-05/aarch64(built at ...) (REV: 00.05.00)\n'
		   '%NET2SER-I:  Added device #00 [</dev/ttyUSB0>, Chars: <9600, 8, N, 1>, Flow: <NONE>, ...]\n'
		   '%NET2SER-I:  Added listener #00 [Target: </dev/ttyUSB0>, Net: <TCP:0.0.0.0:5000>, ...]\n'
		   '%N2S-S-DEVREADY, Device </dev/ttyUSB0> [9600 baud, 8N1, flow: NONE] --- is ready\n'
		   '%N2S-S-LSNRRDY, [#3] Listener 0.0.0.0:5000 [Target: </dev/ttyUSB0>] --- is ready'),
		 P('Two lines matter most: <b>DEVREADY</b> (the port is open) and <b>LSNRRDY</b> (the TCP '
		   'port is listening). If you see both, the gateway is up.'),
		 P('Inside, the two directions are decoupled by a pair of ring buffers, and the poll() '
		   'events are derived from their state at every round --- this is what gives the natural '
		   'back pressure to a fast side of the pair (Figure 3).'),
		 fig(dia_rings(L), 'The ring buffers and the derived poll() events'),
		 P('To stop the gateway press Ctrl/C once and wait a second. To toggle the tracing of a '
		   '<i>running</i> gateway without a restart:'),
		 C('$ kill -USR1 <pid>'),
		 H2('4.1.  The First Test'),
		 C('$ telnet 127.0.0.1 5000'),
		 P('Press Enter a couple of times --- a console device usually answers with its prompt. '
		   '<font face="Mono">nc</font> works too and is friendlier for the binary data:'),
		 C('$ nc 127.0.0.1 5000')],
		[H1('Chapter 5.  Reading the Log'),
		 fig(dia_msg_anatomy(L), 'The anatomy of a log message'),
		 P('The severity letter is: <b>S</b> --- success, <b>I</b> --- information, <b>W</b> --- '
		   'warning, <b>E</b> --- error, <b>F</b> --- fatal. Grep the log by the code --- this is '
		   'exactly why the codes exist:'),
		 C('$ grep DEVBUSY /var/log/net2serial.log')],
		[H1('Chapter 6.  Troubleshooting'),
		 P('<b>The golden rule: run with /trace and read the log. The gateway always says what it '
		   'dislikes.</b>'),
		 H2('6.1.  The Symptom, the Cause, the Action'),
		 tbl(['You see', 'It means / what to do'], [
			('%N2S-E-DEVOPNERR, ... errno: 2', 'The port file does not exist: the adapter is unplugged '
				'or the name is wrong. Run dmesg | tail after plugging it in; fix device in the settings.'),
			('%N2S-E-DEVOPNERR, ... errno: 13', 'Permission denied. Run with sudo, or add yourself to '
				'the dialout group.'),
			('%N2S-E-DEVOPNERR, ... errno: 16', 'The port is busy: another program holds it. '
				'Find it: sudo fuser /dev/ttyUSB0.'),
			('%N2S-W-DEVBUSY, ...', 'Somebody is already working with that port. This is by design '
				'(Section 1.1). Wait, or ask the colleague to disconnect.'),
			('%N2S-E-LSNRERR, ... errno: 98', 'The TCP port is taken: a second gateway instance or '
				'another program. Find it: sudo ss -tlnp | grep 5000.'),
			('%N2S-E-LSNRERR, ... errno: 13', 'The ports below 1024 need root. Run with sudo, or take '
				'a port above 1024.'),
			('%N2S-E-LINKDOWN, ...', 'The serial port has died under a live session --- almost always '
				'an unplugged USB adapter. Plug it back and reconnect.'),
			('%N2S-W-SESSTMO, ...', 'The session was idle for 20 minutes and was closed. Only truly '
				'idle sessions are closed --- any traffic resets the timer.'),
			('You connect, but nothing comes back', '1) The wrong speed or parity --- re-check chars; '
				'2) the wiring (a null-modem cable is often needed); 3) the device is off or has '
				'nothing to say --- press Enter.'),
			('The output is garbage characters', 'Almost always the wrong speed. Try 9600, 19200, '
				'38400, 115200 in turn.'),
			('A long paste comes out truncated', 'The device cannot keep up: set flow = "RTS/CTS" or '
				'"XON/XOFF" per the device manual.'),
			('... out of range [a..b]', 'A settings value is out of its range; the allowed range is '
				'right in the message.'),
			('No serials has been defined!', 'Not a single serials record survived the validation. '
				'Read the error lines above --- each rejected record says why.'),
		 ], [50*mm, 113*mm], a_mono_cols=(0,)),
		 H2('6.2.  The Message Codes Reference'),
		 tbl(['Code', 'Sev', 'When it appears'], [
			('REVISNF', 'I', 'At the start: the program version. Quote it when asking for help.'),
			('DEVREADY', 'S', 'The serial port is open; the actual line parameters are shown.'),
			('LSNRRDY', 'S', 'The TCP port is listening; the address, the port and the target device '
				'are shown.'),
			('DEVOPNERR', 'E', 'The serial port cannot be opened; errno says why (2 = no such file, '
				'13 = the permissions, 16 = busy).'),
			('LSNRERR', 'E', 'bind()/listen() failed for a TCP port; errno says why (98 = taken, '
				'13 = the privileges).'),
			('NETCONN', 'S', 'A client has connected; its address:port and the listener are shown.'),
			('NETDISCN', 'S', 'A client has disconnected; its address:port is shown.'),
			('DEVBUSY', 'W', 'A client was rejected: the port is already owned by another session.'),
			('SESSTMO', 'W', 'A session was closed after being idle for too long.'),
			('NETIOERR', 'E', 'A network I/O error; the failed call and the errno are shown.'),
			('TTYIOERR', 'E', 'A serial I/O error; the failed call and the errno are shown.'),
			('LINKDOWN', 'E', 'The serial line failed under a live session (an unplugged adapter).'),
			('EXITST', 'I', 'The gateway exits; the exit flag and the final status are shown.'),
		 ], [26*mm, 10*mm, 127*mm], a_mono_cols=(0,)),
		 H2('6.3.  If Nothing Helps'),
		 P('Collect and attach to your question: 1) the full start-up log with /trace (from REVISNF '
		   'to the first error); 2) your settings file; 3) the output of <font face="Mono">ls -l '
		   '&lt;device&gt;</font> and <font face="Mono">dmesg | tail -20</font>; 4) the exact model '
		   'of the device and its documented line parameters. With these four things the problem is '
		   'almost always visible at a glance.')],
		]
	L['chapters'] = chapters
	return L

# ----------------------------------------------------------------------------------------------
#	The RUSSIAN language pack
# ----------------------------------------------------------------------------------------------
def s_pack_ru ():
	L = {}
	L['doctitle'] = 'Руководство пользователя'
	L['runhead']  = 'net2serial — Руководство пользователя'
	L['titleblock'] = [
		('Номер документа:', 'DO-N2SUG-RU-01A'),
		('Дата публикации:', PUBDATE_RU),
		('Сведения о ревизии:', 'Это новое руководство.'),
		('Операционная система:', 'Linux (glibc), ядро 4.x и новее'),
		('Версия программы:', 'net2serial ' + VERSION),
	]
	L['copyright'] = 'Copyright © 2026 %s' % ORG
	L['legal_h'] = 'Правовая информация'
	L['legal'] = [
		'Сведения в настоящем документе могут быть изменены без предварительного уведомления. '
		'%s не несёт ответственности за технические или редакционные ошибки и пропуски в настоящем документе.' % ORG,
		'Программа, описанная в настоящем руководстве, распространяется в надежде, что она будет полезной, '
		'но БЕЗ КАКИХ-ЛИБО ГАРАНТИЙ, в том числе без подразумеваемых гарантий товарной пригодности '
		'и пригодности для конкретной цели.',
		'Linux — зарегистрированный товарный знак Линуса Торвальдса. UNIX — зарегистрированный '
		'товарный знак The Open Group. Прочие товарные знаки принадлежат их владельцам.',
	]
	L['toc_h'] = 'Содержание'
	L['preface_h'] = 'Предисловие'
	L['preface'] = [
		('О настоящем руководстве', [
			'Настоящее руководство описывает настройку, эксплуатацию и поиск неисправностей '
			'<b>net2serial</b> — шлюза, который передаёт сырой поток октетов между TCP-соединением '
			'и локальным последовательным портом, в обе стороны и не интерпретируя его.']),
		('Для кого написано', [
			'Руководство рассчитано на читателя без опыта работы с последовательными линиями и '
			'администрирования Linux: каждый шаг расписан, ничего не подразумевается. Опытный '
			'инженер может сразу перейти к главе 3 (файл настроек) и главе 6 (поиск неисправностей).']),
		('Структура документа', [
			'Глава 1 объясняет, что делает шлюз и почему порт отдаётся одному клиенту. Глава 2 — '
			'контрольный список перед запуском. Глава 3 описывает файл настроек ключ за ключом. '
			'Глава 4 — запуск, опции командной строки и первая проверка. Глава 5 учит читать журнал. '
			'Глава 6 — справочник по поиску неисправностей: симптом, причина, действие, и справочник '
			'всех кодов сообщений.']),
		('Смежные документы', [
			'<i>README.md</i> — последовательность установки (пакет StarLet, сборка, '
			'<font face="Mono">make install</font>). Страницы руководства '
			'<font face="Mono">termios</font>(3) и <font face="Mono">stty</font>(1) — параметры '
			'последовательной линии в операционной системе.']),
	]
	L['conv_h'] = 'Соглашения'
	L['conv_c1'] = 'Обозначение'; L['conv_c2'] = 'Значение'
	L['conventions'] = [
		('Моноширинный', 'Примеры кода, имена файлов, команды и экранный вывод.'),
		('%N2S-E-КОД',   'Код сообщения, как он выглядит в журнале шлюза; см. раздел 6.2.'),
		('<...>',        'Место подстановки фактического значения, например имени устройства.'),
		('$',            'Приглашение оболочки непривилегированного пользователя; вводить его не нужно.'),
		('Ctrl/C',       'Удерживая клавишу Ctrl, нажмите клавишу C.'),
	]
	L.update({
		'dia_client': 'TCP-клиент', 'dia_clienttool': 'telnet, nc, терминал',
		'dia_oneowner': '(один владелец за раз)', 'dia_device': 'Ваше устройство',
		'dia_devicekind': 'консоль, ПЛК, ИБП ...', 'dia_octets': 'сырой поток октетов',
		'dia_client2': 'Второй TCP-клиент', 'dia_rejected': 'получает отказ: DEVBUSY',
		'dia_socket': 'TCP-сокет', 'dia_port': 'Порт',
		'dia_ringnet': 'Кольцо сетевых данных', 'dia_ringtty': 'Кольцо данных порта',
		'dia_ringtitle': 'События poll() выводятся из состояния колец',
		'dia_rule1': 'Есть место в кольце  ->  просим POLLIN у его источника',
		'dia_rule2': 'Есть данные в кольце  ->  просим POLLOUT у его приёмника',
		'dia_free': 'Порт свободен', 'dia_owned': 'Занят сессией #N',
		'dia_freed': 'Порт снова свободен', 'dia_busy': 'Второй клиент: DEVBUSY',
		'dia_accept': 'accept + захват', 'dia_disc': 'отключение + освобождение',
		'dia_second': 'отказ, владельца не трогаем',
		'dia_owntitle': 'Жизненный цикл владения портом',
		'dia_fac': 'подсистема', 'dia_sev': 'серьёзность: S I W E F',
		'dia_code': 'код сообщения (раздел 6.2)',
		'dia_msgline': 'Каждое важное событие — одна строка с кодом:',
	})
	L['fig_w'] = 'Рисунок %d.  '

	def chapters (fig):
		return [
		[H1('Глава 1.  Что делает шлюз'),
		 P('У вас есть устройство с последовательной консолью или последовательным интерфейсом — '
		   'коммутатор, маршрутизатор, старый VAX, ПЛК, ИБП. Оно воткнуто в последовательный порт '
		   'компьютера, на котором работает шлюз. И вы хотите добраться до этого устройства по сети, '
		   'со своего рабочего места. Шлюз стоит между ними и пропускает октеты насквозь, как '
		   'показано на рисунке 1.'),
		 fig(dia_architecture(L), 'Путь данных через шлюз'),
		 P('Ничего не интерпретируется: что вы набрали — уходит в устройство как есть, что устройство '
		   'напечатало — приходит обратно как есть. Именно поэтому работает с чем угодно: с '
		   'приглашением логина, с загрузчиком, с двоичным протоколом.'),
		 H2('1.1.  Один клиент на порт за раз'),
		 P('У потока октетов нет разбивки на кадры. Если бы два человека печатали в одну консоль '
		   'одновременно, их символы перемешались бы и ни один не получил бы читаемого ответа. '
		   'Поэтому порт отдаётся одной сессии: второй клиент получает внятный отказ, а первый '
		   'продолжает работать, ничего не заметив. Как только первый отключится — порт свободен '
		   'для следующего, см. рисунок 2.'),
		 fig(dia_ownership(L), 'Жизненный цикл владения портом')],
		[H1('Глава 2.  Перед началом'),
		 P('Пройдите этот список один раз — он сэкономит час гаданий потом.'),
		 P('<b>1.</b> Шлюз установлен (см. <i>README.md</i>). После установки у вас есть программа '
		   '<font face="Mono">/usr/local/sbin/net2serial</font> и файл настроек '
		   '<font face="Mono">/usr/local/etc/net2serial/net2serial_settings.conf</font>.'),
		 P('<b>2.</b> Вы знаете, к какому последовательному порту подключено устройство. В Linux это '
		   'файл вида <font face="Mono">/dev/ttyS0</font> или <font face="Mono">/dev/ttyUSB0</font>. '
		   'Если не уверены — выдерните и вставьте USB-адаптер и выполните:'),
		 C('$ dmesg | tail'),
		 P('<b>3.</b> Вы знаете параметры линии устройства: скорость, биты данных, чётность, стоп-биты '
		   'и управление потоком. Они написаны в паспорте устройства; самый распространённый '
		   'консольный набор — <font face="Mono">9600, 8, N, 1</font> без управления потоком.'),
		 P('<b>4.</b> Ваш пользователь имеет право открыть порт. Проверьте группу файла порта:'),
		 C('$ ls -l /dev/ttyUSB0\n'
		   'crw-rw---- 1 root dialout 188, 0 ... /dev/ttyUSB0'),
		 P('Если группа <font face="Mono">dialout</font> — добавьте себя в неё и перезайдите в систему '
		   '(либо просто запускайте шлюз через <font face="Mono">sudo</font>):'),
		 C('$ sudo usermod -a -G dialout $USER')],
		[H1('Глава 3.  Файл настроек'),
		 P('В файле два раздела: <font face="Mono">serials</font> (последовательные порты) и '
		   '<font face="Mono">listeners</font> (TCP-порты). Минимальный рабочий пример:'),
		 C('serials = (\n'
		   '\t{\tdevice = "/dev/ttyUSB0";\n'
		   '\t\tchars  = "9600, 8, N, 1";\n'
		   '\t\tflow   = "NONE";\n'
		   '\t}\n'
		   ');\n\n'
		   'listeners = (\n'
		   '\t{\tbind   = "TCP:0.0.0.0:5000";\n'
		   '\t\ttarget = "/dev/ttyUSB0";\n'
		   '\t}\n'
		   ');'),
		 P('Читается так: <i>«открой /dev/ttyUSB0 на 9600-8-N-1 без управления потоком; слушай '
		   'TCP-клиентов на всех интерфейсах, порт 5000, и соединяй их с этим портом»</i>.'),
		 H2('3.1.  Раздел serials'),
		 tbl(['Ключ', 'Обяз.', 'Значение и допустимые величины'], [
			('device', 'да', 'Файл последовательного порта, например /dev/ttyUSB0.'),
			('chars', 'да', 'Параметры линии: скорость, биты данных, чётность, стоп-биты. '
				'Скорость 50..4000000; биты данных 5..8; чётность N (нет), E (чётная), O (нечётная); '
				'стоп-биты 1..2.'),
			('flow', 'нет', 'Управление потоком: NONE, XON/XOFF, RTS/CTS. По умолчанию NONE.'),
			('iotmo', 'нет', 'Таймаут ввода-вывода, миллисекунды, 1..600000. По умолчанию 3000.'),
			('rs485', 'нет', '1 — просить ядро управлять направлением RS-485. По умолчанию 0.'),
			('desc', 'нет', 'Свободное описание, для вашего собственного удобства.'),
		 ], [26*mm, 16*mm, 121*mm], a_mono_cols=(0,)),
		 P('<b>Какое управление потоком выбрать?</b> Если в паспорте устройства ничего не сказано — '
		   'NONE. Консольный кабель в стиле Cisco обычно хочет RTS/CTS. Старые терминалы и принтеры '
		   'часто хотят XON/XOFF. Если вы вставляете длинный текст и хвост приходит покорёженным — '
		   'управление потоком проверяйте первым делом.'),
		 P('Если запись неправильная, шлюз пропускает её и говорит почему — с допустимым диапазоном '
		   'прямо в сообщении:'),
		 C('%NET2SER-E:  [serial #00:</dev/ttyUSB0>] --- speed 31 baud is out of range [50..4000000]'),
		 H2('3.2.  Раздел listeners'),
		 tbl(['Ключ', 'Обяз.', 'Значение и допустимые величины'], [
			('bind', 'да', 'Где слушать: TCP:&lt;IP-адрес&gt;:&lt;порт&gt;. Порт 1..65535; '
				'0.0.0.0 — все интерфейсы. UDP не поддерживается.'),
			('target', 'да', 'С каким последовательным портом соединять. Должен дословно, символ в '
				'символ, совпадать с device из serials.'),
			('connlm', 'нет', 'Очередь ожидания у слушающего сокета, 1..128. Это НЕ число '
				'одновременных клиентов — см. раздел 1.1.'),
			('iotmo', 'нет', 'Сетевой таймаут ввода-вывода, миллисекунды, 1..600000.'),
		 ], [26*mm, 16*mm, 121*mm], a_mono_cols=(0,)),
		 P('Порты ниже 1024 в Linux требуют прав root. Не хотите работать под root — возьмите, '
		   'например, <font face="Mono">5000</font> и укажите его в клиенте.')],
		[H1('Глава 4.  Запуск шлюза'),
		 C('$ /usr/local/sbin/net2serial /settings=/usr/local/etc/net2serial/net2serial_settings.conf'),
		 tbl(['Опция', 'Смысл'], [
			('/settings=&lt;файл&gt;', 'Файл настроек (глава 3).'),
			('/trace', 'Подробная трассировка: сообщается каждая порция октетов. Бесценно при первом запуске.'),
			('/logfile=&lt;файл&gt;', 'Писать журнал в файл вместо экрана.'),
			('/logsize=&lt;октет&gt;', 'Ротировать файл журнала при превышении размера.'),
		 ], [42*mm, 121*mm], a_mono_cols=(0,)),
		 P('Здоровый запуск печатает (сокращённо):'),
		 C('%N2S-I-REVISNF, Rev: NET2SER X.00-05/aarch64(built at ...) (REV: 00.05.00)\n'
		   '%NET2SER-I:  Added device #00 [</dev/ttyUSB0>, Chars: <9600, 8, N, 1>, Flow: <NONE>, ...]\n'
		   '%NET2SER-I:  Added listener #00 [Target: </dev/ttyUSB0>, Net: <TCP:0.0.0.0:5000>, ...]\n'
		   '%N2S-S-DEVREADY, Device </dev/ttyUSB0> [9600 baud, 8N1, flow: NONE] --- is ready\n'
		   '%N2S-S-LSNRRDY, [#3] Listener 0.0.0.0:5000 [Target: </dev/ttyUSB0>] --- is ready'),
		 P('Важнее всего две строки: <b>DEVREADY</b> (порт открыт) и <b>LSNRRDY</b> (TCP-порт '
		   'слушает). Видите обе — шлюз работает.'),
		 P('Внутри две стороны развязаны парой кольцевых буферов, а события poll() выводятся из их '
		   'состояния на каждом круге — именно это даёт естественное встречное давление быстрой '
		   'стороне пары (рисунок 3).'),
		 fig(dia_rings(L), 'Кольцевые буферы и выводимые из них события poll()'),
		 P('Остановка: один раз Ctrl/C и секунда ожидания. Переключить трассировку у <i>уже '
		   'работающего</i> шлюза без перезапуска:'),
		 C('$ kill -USR1 <pid>'),
		 H2('4.1.  Первая проверка'),
		 C('$ telnet 127.0.0.1 5000'),
		 P('Нажмите Enter пару раз — консольное устройство обычно отвечает своим приглашением. '
		   '<font face="Mono">nc</font> тоже подходит и дружелюбнее к двоичным данным:'),
		 C('$ nc 127.0.0.1 5000')],
		[H1('Глава 5.  Как читать журнал'),
		 fig(dia_msg_anatomy(L), 'Анатомия сообщения журнала'),
		 P('Буква серьёзности: <b>S</b> — успех, <b>I</b> — информация, <b>W</b> — предупреждение, '
		   '<b>E</b> — ошибка, <b>F</b> — фатально. Ищите в журнале по коду — именно для этого коды '
		   'и существуют:'),
		 C('$ grep DEVBUSY /var/log/net2serial.log')],
		[H1('Глава 6.  Поиск неисправностей'),
		 P('<b>Золотое правило: запустите с /trace и читайте журнал. Шлюз всегда говорит, что именно '
		   'ему не нравится.</b>'),
		 H2('6.1.  Симптом, причина, действие'),
		 tbl(['Вы видите', 'Что это значит / что делать'], [
			('%N2S-E-DEVOPNERR, ... errno: 2', 'Файла порта нет: адаптер выдернут или имя неверное. '
				'Выполните dmesg | tail после втыкания; поправьте device в настройках.'),
			('%N2S-E-DEVOPNERR, ... errno: 13', 'Нет прав доступа. Запустите под sudo или добавьте '
				'себя в группу dialout.'),
			('%N2S-E-DEVOPNERR, ... errno: 16', 'Порт занят другой программой. '
				'Найдите её: sudo fuser /dev/ttyUSB0.'),
			('%N2S-W-DEVBUSY, ...', 'С этим портом уже кто-то работает. Так задумано (раздел 1.1). '
				'Подождите или попросите коллегу отключиться.'),
			('%N2S-E-LSNRERR, ... errno: 98', 'TCP-порт занят: второй экземпляр шлюза или другая '
				'программа. Найдите: sudo ss -tlnp | grep 5000.'),
			('%N2S-E-LSNRERR, ... errno: 13', 'Порты ниже 1024 требуют root. Запустите под sudo или '
				'возьмите порт выше 1024.'),
			('%N2S-E-LINKDOWN, ...', 'Последовательный порт умер под живой сессией — почти всегда '
				'выдернутый USB-адаптер. Воткните обратно и переподключитесь.'),
			('%N2S-W-SESSTMO, ...', 'Сессия простояла без дела 20 минут и была закрыта. Закрываются '
				'только по-настоящему простаивающие сессии — любой трафик сбрасывает таймер.'),
			('Подключились, но ничего не приходит', '1) Не та скорость или чётность — перепроверьте '
				'chars; 2) провода (часто нужен нуль-модемный кабель); 3) устройство выключено или '
				'ему нечего сказать — нажмите Enter.'),
			('В выводе мусорные символы', 'Почти всегда не та скорость. Пробуйте по очереди 9600, '
				'19200, 38400, 115200.'),
			('Длинная вставка приходит обрезанной', 'Устройство не успевает: поставьте flow = "RTS/CTS" '
				'или "XON/XOFF" по паспорту устройства.'),
			('... out of range [a..b]', 'Значение в настройках вне диапазона; допустимый диапазон '
				'написан прямо в сообщении.'),
			('No serials has been defined!', 'Ни одна запись serials не прошла проверку. Читайте '
				'строки ошибок выше — каждая отброшенная запись объясняет причину.'),
		 ], [50*mm, 113*mm], a_mono_cols=(0,)),
		 H2('6.2.  Справочник кодов сообщений'),
		 tbl(['Код', 'Сер.', 'Когда появляется'], [
			('REVISNF', 'I', 'При старте: версия программы. Указывайте её, когда просите помощи.'),
			('DEVREADY', 'S', 'Последовательный порт открыт; показаны фактические параметры линии.'),
			('LSNRRDY', 'S', 'TCP-порт слушает; показаны адрес, порт и целевое устройство.'),
			('DEVOPNERR', 'E', 'Последовательный порт не открывается; errno объясняет почему '
				'(2 — файла нет, 13 — права, 16 — занят).'),
			('LSNRERR', 'E', 'Отказ bind()/listen() для TCP-порта; errno объясняет почему '
				'(98 — занят, 13 — привилегии).'),
			('NETCONN', 'S', 'Клиент подключился; показаны его адрес:порт и listener.'),
			('NETDISCN', 'S', 'Клиент отключился; показан его адрес:порт.'),
			('DEVBUSY', 'W', 'Клиенту отказано: порт уже принадлежит другой сессии.'),
			('SESSTMO', 'W', 'Сессия закрыта после слишком долгого простоя.'),
			('NETIOERR', 'E', 'Ошибка сетевого ввода-вывода; показаны отказавший вызов и errno.'),
			('TTYIOERR', 'E', 'Ошибка последовательного ввода-вывода; показаны отказавший вызов и errno.'),
			('LINKDOWN', 'E', 'Последовательная линия отказала под живой сессией (выдернутый адаптер).'),
			('EXITST', 'I', 'Шлюз завершается; показаны флаг выхода и итоговый статус.'),
		 ], [26*mm, 11*mm, 126*mm], a_mono_cols=(0,)),
		 H2('6.3.  Если ничего не помогло'),
		 P('Соберите и приложите к вопросу: 1) полный журнал запуска с /trace (от REVISNF до первой '
		   'ошибки); 2) ваш файл настроек; 3) вывод <font face="Mono">ls -l &lt;устройство&gt;</font> '
		   'и <font face="Mono">dmesg | tail -20</font>; 4) точную модель устройства и его паспортные '
		   'параметры линии. С этими четырьмя вещами проблема почти всегда видна с первого взгляда.')],
		]
	L['chapters'] = chapters
	return L


if __name__ == '__main__':
	l_lang, l_out = sys.argv[1], sys.argv[2]
	build (s_pack_en() if l_lang == 'EN' else s_pack_ru(), l_out)
	print ('%s -> %s' % (l_lang, l_out))
