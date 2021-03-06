
// EkbEdit.cpp : implementation file
//

#include <list>
#include <string>
#include "stdafx.h"

#include "runbhjrun.h"

#include <list>
#include <string>
#include "EkbEdit.h"
#include "bhjlib.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <boost/regex.hpp>

using namespace boost;
#include <iostream>
#include <string>
#define ENABLE_BHJDEBUG
#include "bhjdebug.h" 
#include <commctrl.h>
#include <map>
#include <sys/types.h>
#include <sys/stat.h>
#include <vector>
using std::vector;

using std::map;

using namespace bhj;
using std::list;


#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CEkbEdit

CEkbEdit::CEkbEdit()
{
	m_skip_onchange = false;
	m_listBox = NULL;
	m_simpleWnd = NULL;
	m_id = 0;
	m_strHistFile = "";
	m_mark = -1;
	m_find_mode = mode_use_nothing;
	m_use_history = true;
}

CEkbEdit::~CEkbEdit()
{
}


BEGIN_MESSAGE_MAP(CEkbEdit, CRichEditCtrl)
	//{{AFX_MSG_MAP(CEkbEdit)
	ON_WM_KILLFOCUS()
	ON_WM_SETFOCUS()
	ON_WM_CREATE()
	ON_WM_WINDOWPOSCHANGING()
	ON_WM_CTLCOLOR_REFLECT()
	ON_CONTROL_REFLECT(EN_HSCROLL, OnHscroll)
	//}}AFX_MSG_MAP
	ON_CONTROL_REFLECT_EX(EN_CHANGE, &CEkbEdit::OnEnChange)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CEkbEdit message handlers

specKeyState_t CEkbEdit::getSpecKeyState()
{
	int skState = eNone;
	if (GetKeyState(VK_CONTROL)<0) {
		skState |= eCtrl;
	}

	if (GetKeyState(VK_MENU) < 0) {
		skState |= eAlt;
	}

	if (GetKeyState(VK_SHIFT) < 0) {
		skState |= eShift;
	}

	return (specKeyState_t) skState;
}

void CEkbEdit::setListBox(CHListBox& listBox)
{
	if (m_simpleWnd) {
		return;
	}
	m_listBox = &listBox;
}

void CEkbEdit::createListBox()
{
	if (m_listBox) {
		BHJDEBUG(" already has a listbox");
		return;
	}

	m_simpleWnd = new CEkbHistWnd(this);

	m_listBox = m_simpleWnd->m_listBox;
}


CRect GetWindowRect(CWnd* wnd)
{
	CRect rect;
	wnd->GetWindowRect(&rect);
	return rect;
}

CRect GetClientRect(CWnd* wnd)
{
	CRect rect;
	wnd->GetClientRect(&rect);
	return rect;
}

HWND getTopParentHwnd(CWnd* wnd)
{
	if (!wnd || !wnd->GetParentOwner()) {
		return NULL;
	}

	return wnd->GetParentOwner()->m_hWnd;
}

void CEkbEdit::selectPrevItem(int prev)
{
	if (!m_listBox) {
		return;
	}

	if (m_simpleWnd && !m_simpleWnd->IsWindowVisible()) {
		fillListBox(getText());
		m_simpleWnd->show();
	}

	if (m_listBox->GetCount() == 0) {
		return;
	}

	if (m_listBox->GetCurSel() >= 0) {
		int i = m_listBox->GetCurSel();
		m_listBox->SetCurSel(-1);
		if (prev) {
			i = (i + m_listBox->GetCount() - 1) % m_listBox->GetCount();
		} else {
			i = (i+1) % m_listBox->GetCount();
		}		
		int ret = m_listBox->SetCurSel(i);

	} else {
		if (prev) {
			m_listBox->SetCurSel(m_listBox->GetCount()-1);
		} else {
			m_listBox->SetCurSel(0);
		}
	}
	SetWindowText(getSelectedText());
}

void CEkbEdit::selectNextItem()
{
	selectPrevItem(0);
}

void CEkbEdit::move_to(int pos)
{
	if (pos < 0) {
		pos = 0;
	}
	
	if (pos > GetLength()) {
		pos = GetLength();
	}

	if (m_mark >= 0) {
		SetSel(m_mark, pos); 
	} else {
		SetSel(pos, pos);
	}
}

void CEkbEdit::getTextFromSelectedItem()
{
	SetWindowText(getSelectedText());
	keyboard_quit();
	move_to(GetLength());
}

int CEkbEdit::getPoint()
{
	long start, end;
	CRichEditCtrl::GetSel(start, end);
	CPoint pt = GetCaretPos();
	CPoint pts = PosFromChar(start);
	CPoint pte = PosFromChar(end);

	int sdelta = abs(pt.x-pts.x);
	int edelta = abs(pt.x-pte.x);

	if (sdelta < edelta) {
		return start;
	} else {
		return end;
	}
}

void CEkbEdit::SetWindowText(const cstring& str)
{
	m_skip_onchange = true;
	CWnd::SetWindowText(str.c_str());
	m_skip_onchange = false;
	//keyboard_quit(); can't use this one, since it will call the switch_find_mode(), which in turn calls fillListBox();
	SetSel(GetLength(), GetLength());
	m_mark = -1;
	
	move_to(GetLength());
}

void CEkbEdit::endOfLine()
{
	move_to(GetLength());
}

void CEkbEdit::beginOfLine()
{
	move_to(0);
}

void CEkbEdit::killEndOfLine()
{
	delete_range(getPoint(), GetLength());
}

void CEkbEdit::killBeginOfLine()
{
	delete_range(0, getPoint());
}

void CEkbEdit::forwardChar()
{

	if (getPoint() < GetLength()) {
		move_to(getPoint() + 1);
	}
}

void CEkbEdit::backwardChar()
{
	if (getPoint() > 0) {
		move_to(getPoint() - 1);
	}
}

int CEkbEdit::GetLength()
{
	CString text;
	GetWindowText(text);
	return text.GetLength();
}

void CEkbEdit::deleteChar()
{
	int end = getPoint();
	if (end < GetLength()) {
		delete_range(end, end+1);
	}
}


void CEkbEdit::backwardWord()
{
	int start = getPoint();
	CString text;
	GetWindowText(text);
	enum {
		eInWord,
		eOutWord,
	};
	int state = eOutWord;
	for (int i=start-1; i>=0; i--) {
		if (state == eInWord && !isalnum(text[i])) {
			move_to(i+1);
			return;
		} else if (isalnum(text[i])) {
			state = eInWord;
		}
	}		
	move_to(0);
}

void CEkbEdit::delete_range(int start, int end)
{
	keyboard_quit();
	SetSel(start, end);
	Clear();
}

void CEkbEdit::backwardKillWord()
{
	int start = getPoint();
	CString text;
	GetWindowText(text);
	enum {
		eInWord,
		eOutWord,
	};
	int state = eOutWord;
	for (int i=start-1; i>=0; i--) {
		if (state == eInWord && !isalnum(text[i])) {
			delete_range(i+1, start);
			return;
		} else if (isalnum(text[i])) {
			state = eInWord;
		}
	}		
	delete_range(0, start);
}

void CEkbEdit::forwardWord()
{
	int end = getPoint();
	CString text;
	GetWindowText(text);
	enum {
		eInWord,
		eOutWord,
	};
	int state = eOutWord;
	for (int i=end+1; i<GetLength(); i++) {
		if (state == eInWord && !isalnum(text[i])) {
			move_to(i);
			return;
		} else if (isalnum(text[i])) {
			state = eInWord;
		}
	}
	move_to(GetLength());
}

void CEkbEdit::forwardKillWord()
{
	int end = getPoint();

	CString text;
	GetWindowText(text);
	enum {
		eInWord,
		eOutWord,
	};
	int state = eOutWord;
	for (int i=end+1; i<GetLength(); i++) {
		if (state == eInWord && !isalnum(text[i])) {
			delete_range(end, i);
			return;
		} else if (isalnum(text[i])) {
			state = eInWord;
		}
	}
	delete_range(end, GetLength());
}

cstring CEkbEdit::getSelectedText()
{
	if (!m_listBox || !m_listBox->GetCount()) {
		return "";
	}

	if (m_listBox->GetCurSel() < 0) {
		return "";
	}

	int i = m_listBox->GetCurSel();
	CString text;
	m_listBox->GetText(i, text);
	return text;
}

void CEkbEdit::escapeEdit()
{

	m_find_mode = mode_use_nothing;
	if (m_simpleWnd && m_simpleWnd->IsWindowVisible()) {
		if (m_listBox->GetCurSel() > 0) {
			m_listBox->SetCurSel(0);
			SetWindowText(getSelectedText());
			return;
		}
		m_simpleWnd->hide();
		if (m_listBox) {
			m_listBox->SetCurSel(-1);
		}
		return;
	}

	delete_range(0, GetLength());
	keyboard_quit();

}

void CEkbEdit::set_mark_command()
{
	m_mark = getPoint();
}

void CEkbEdit::keyboard_quit()
{
	SetSel(getPoint(), getPoint());
	switch_find_mode(mode_use_nothing);
	m_mark = -1;
}

void CEkbEdit::exchange_point_and_mark()
{
	int point = getPoint();
	int tmp = m_mark;
	m_mark = point;
	move_to(tmp);
}

void CEkbEdit::kill_region()
{
	Cut();
	keyboard_quit();
}

void CEkbEdit::kill_ring_save()
{
	Copy();
	keyboard_quit();
}

void CEkbEdit::yank()
{
	Paste();
	keyboard_quit();
}

void CEkbEdit::undo()
{
	Undo();
	keyboard_quit();
}

void CEkbEdit::redo()
{
	Redo();
	keyboard_quit();
}

void CEkbEdit::toggle_history()
{
	m_use_history = !m_use_history;
	if (m_simpleWnd && m_simpleWnd->IsWindowVisible()) {
		fillListBox(getText());
	}
}

void CEkbEdit::switch_find_mode(int mode)
{
	if (mode < 0) {
		m_find_mode = (m_find_mode+1)%mode_last_not_used;
	} else {
		m_find_mode = mode;
	}
	if (m_find_mode == mode_use_locate) {
		cmdline_parser cp(getText());
		m_histList.push_front(getText());
		m_histList = unique_ls(m_histList);
		saveHist();
		getLocateMatchingFiles(cp.get_args(), true);
	}
	if (m_simpleWnd && m_simpleWnd->IsWindowVisible()) {
		fillListBox(getText());
	}
}

void CEkbEdit::scroll_up()
{
	if (m_listBox) {
		m_listBox->SendMessage(WM_VSCROLL, SB_PAGEDOWN, 0);
		int top = m_listBox->GetTopIndex();
		m_listBox->SetCurSel(top);
	}
}

void CEkbEdit::scroll_down()
{
	if (m_listBox) {
		m_listBox->SendMessage(WM_VSCROLL, SB_PAGEUP, 0);
		int top = m_listBox->GetTopIndex();
		m_listBox->SetCurSel(top);
	}				  
}

void CEkbEdit::end_of_lb()
{
	if (m_listBox) {
		m_listBox->SendMessage(WM_VSCROLL, SB_BOTTOM, 0);
		m_listBox->SendMessage(WM_HSCROLL, SB_RIGHT, 0);
		int count = m_listBox->GetCount();
		m_listBox->SetCurSel(count-1);
	}
}

void CEkbEdit::scroll_left()
{
	if (m_listBox) {
		m_listBox->SendMessage(WM_HSCROLL, SB_LINERIGHT, 0);
	}
}

void CEkbEdit::scroll_right()
{
	if (m_listBox) {
		m_listBox->SendMessage(WM_HSCROLL, SB_LINELEFT, 0);
	}
}

void CEkbEdit::begin_of_lb()
{
	if (m_listBox) {
		m_listBox->SendMessage(WM_VSCROLL, SB_TOP, 0);
		m_listBox->SetCurSel(0);
	}
}

bool want_debug_key(int vk)
{
	int ndks[] = {
		VK_LEFT,
		VK_RIGHT,
		VK_SHIFT,
		VK_CONTROL,
		VK_MENU,
		VK_LCONTROL, 
		VK_LSHIFT,
		VK_LMENU,
		0
	};
	for (int i=0; ndks[i]; i++) {
		if (vk == ndks[i]) {
			return false;
		}
	}
	return true;		
}

BOOL CEkbEdit::PreTranslateMessage(MSG* pMsg) 
{
	CString text;
	this->GetWindowText(text);
	char head=0,second=0;
	if(text.GetLength()>0) head=text.GetAt(0);
	if(text.GetLength()>1) second=text.GetAt(1);
	bool bCut=true;

	// if (pMsg->message == WM_KEYDOWN || pMsg->message == WM_SYSKEYDOWN ||
	// 	pMsg->message == WM_KEYUP || pMsg->message == WM_SYSKEYUP) {
	// 	BYTE kb[256];
	// 	GetKeyboardState(kb);
	// 	for (int i=0; i<256; i++) {
	// 		printf("%d ", kb[i]);
	// 		if (i%16 == 0){
	// 			printf("\n");
	// 		}
	// 	}
	// 	printf("\n********************************\n");
	// 	fflush(stdout);
	// }

	// return CRichEditCtrl::PreTranslateMessage(pMsg);

	if (pMsg->message != WM_KEYDOWN && pMsg->message != WM_SYSKEYDOWN) {
		return CRichEditCtrl::PreTranslateMessage(pMsg);
	}

	int debug_key = 1;
#define HandleKey(key, spec, handler, ...) do {							\
		if (!debug_key && want_debug_key(pMsg->wParam)) {			\
			BHJDEBUG(" key is %d, spec is %d",						\
					 pMsg->wParam, getSpecKeyState());				\
		}															\
		debug_key = 1;												\
		if (pMsg->wParam == (key) && getSpecKeyState() == (spec)) {	\
			handler(##__VA_ARGS__);												\
			return true;											\
		}															\
	} while (0)

#define HandleKeyIf(key, spec, handler, cond) do {					\
		if (pMsg->wParam == (key) && getSpecKeyState() == (spec)) {	\
			if (cond) {												\
				handler();											\
				return true;										\
			} else {												\
				return CRichEditCtrl::PreTranslateMessage(pMsg);			\
			}														\
		}															\
	} while (0)


	HandleKey('N', eCtrl, selectNextItem);
	HandleKey('P', eCtrl, selectPrevItem);
	HandleKey(VK_DOWN, eNone, selectNextItem);
	HandleKey(VK_UP, eNone, selectPrevItem);
	HandleKey(VK_RETURN, eCtrl, getTextFromSelectedItem);
	HandleKey('E', eCtrl, endOfLine);
	HandleKey(VK_HOME, eNone, beginOfLine);
	HandleKey(VK_END, eNone, endOfLine);
	HandleKey('A', eCtrl, beginOfLine);
	HandleKey('X', eCtrl, exchange_point_and_mark);
	HandleKey('U', eCtrl, killBeginOfLine);
	HandleKey('K', eCtrl, killEndOfLine);
	HandleKey('F', eCtrl, forwardChar);
	HandleKey('B', eCtrl, backwardChar);
	HandleKey('D', eCtrl, deleteChar);
	HandleKey('B', eAlt, backwardWord);
	HandleKey('F', eAlt, forwardWord);
	HandleKey('D', eAlt, forwardKillWord);
	HandleKey('G', eCtrl, keyboard_quit);
	HandleKey('W', eCtrl, kill_region);
	HandleKey('W', eAlt, kill_ring_save);
	HandleKey('Y', eCtrl, yank);

	
	HandleKey(VK_SPACE, eCtrl, set_mark_command);
	HandleKey(VK_BACK, eAlt, backwardKillWord);
	HandleKey(VK_BACK, eCtrl, backwardKillWord);
	HandleKey(VkKeyScan('/'), eCtrl, undo);
	HandleKey(VkKeyScan('/'), eAlt, redo);
	HandleKey(VkKeyScan('.'), eAltShift, end_of_lb);
	HandleKey(VkKeyScan(','), eAltShift, begin_of_lb);
	HandleKey(VkKeyScan(','), eAlt, scroll_right);
	HandleKey(VkKeyScan('.'), eAlt, scroll_left);
	HandleKey('H', eAlt, toggle_history);
	HandleKey('P', eAlt, switch_find_mode, mode_use_path_env);
	HandleKey('L', eAlt, switch_find_mode, mode_use_locate);
	HandleKey('N', eAlt, switch_find_mode, mode_use_nothing);
	HandleKey('V', eCtrl, scroll_up);
	HandleKey('V', eAlt, scroll_down);

	HandleKeyIf(VK_ESCAPE, eNone, escapeEdit, (GetLength()||(m_simpleWnd&&m_simpleWnd->IsWindowVisible())));


	if (pMsg->wParam == VK_RETURN && getSpecKeyState() == eNone && getSelectedText().size()) {
		SetWindowText(getSelectedText());

		m_histList.push_front(getSelectedText());
		m_histList = unique_ls(m_histList);
		fillListBox("");
		if (m_simpleWnd) {
			m_simpleWnd->hide();
		}
		return CRichEditCtrl::PreTranslateMessage(pMsg);
	}
		
	return CRichEditCtrl::PreTranslateMessage(pMsg);
}

cstring CEkbEdit::getText()
{
	CString text;
	GetWindowText(text);
	return text;
	// long start, end;
	// GetSel(start, end);
	// if (start > end) {
	// 	std::swap(start, end);
	// }
	
	// return text.Mid(0, start) + text.Mid(end, GetLength()-end);
}

void CEkbEdit::saveHist()
{
	if (m_strHistFile.GetLength() == 0) {
		return ;
	}

	FILE* fp = fopen(m_strHistFile, "wb");
	if (!fp) {
		return;
	}

	for (lstring_t::iterator i = m_histList.begin(); i != m_histList.end(); i++) {
		fprintf(fp, "%s\n", i->c_str());
	}
	fclose(fp);
}

BOOL CEkbEdit::OnChange() 
{
	if (m_skip_onchange) {
		return false;
	}
	
	m_find_mode = mode_use_nothing;

	if (m_simpleWnd) {
		if (GetLength()) {
			m_simpleWnd->show();
		} else {
			m_simpleWnd->hide();
		}
	}


	
	fillListBox(getText());
	return false;
}

bool stringContains(const CString& src, const CString& tgt)
{
	return src.Find(tgt) >= 0;
}

lstring_t CEkbEdit::getMatchingStrings(const cstring& text, int point)
{
	lstring_t ls_match;

	cstring left_str = string_left_of(text, point);
	cstring right_str = string_right_of(text, point);

	cmdline_parser cp(left_str);
	lstring_t args = cp.get_args();

	if (m_use_history) {
		for (lstring_t::iterator i = m_histList.begin(); i != m_histList.end(); i++) {
			if (fields_match(*i, args)) {
				ls_match.push_back(*i + right_str);
			}
		}
	}

	if (args.empty()) {
		return ls_match;
	}

	if (bce_dirname(args.back()).c_str()[0] == '.' && args.back().size() < 2) {
		return ls_match;
	}

	if (is_abspath(args.back()) && is_dir_cyg(bce_dirname(args.back()))) {

		BHJDEBUG(" %s is abs", args.back().c_str());
		lstring_t files = getMatchingFiles(bce_dirname(args.back()), bce_basename(args.back()));
		for (lstring_t::iterator i=files.begin(); i!=files.end(); i++) {
			ls_match.insert(ls_match.end(), cp.get_prefix(args.size() - 1) + *i + right_str);
		}

	} else if (m_find_mode == mode_use_path_env && 
		args.front().size() >= 2 && 
		!string_contains(args.front(), "/") && 
		!string_contains(args.front(), "\\")) {
		
		lstring_t files = getPathEnvMatchingFiles(args);
		list_append(ls_match, files);
	} else if (m_find_mode == mode_use_locate &&
			   args.front().size() >= 2) {
		
		lstring_t files = getLocateMatchingFiles(args);
		list_append(ls_match, files);
	}

	return unique_ls(ls_match);
}

void CEkbEdit::fillListBox(const CString& text)
{
	if (!m_listBox) {
		return;
	}
	
	m_listBox->ResetContent();
	m_listBox->AddString(text);
	
	lstring_t ls_match = getMatchingStrings(text, getPoint());
	if (ls_match.size()>1000) {
		ls_match.resize(1000);
		m_listBox->AddString("...");
	}
	for (lstring_t::iterator i = ls_match.begin(); i != ls_match.end(); i++) {
		m_listBox->AddString(CString(cstring(*i)));
	}

	if (m_listBox->GetCount()) {
		m_listBox->SetCurSel(0);
	}
}

int CEkbEdit::setHistFile(const CString& strFileName)
{
// HRESULT SHGetFolderPath(          HWND hwndOwner,
//     int nFolder,
//     HANDLE hToken,
//     DWORD dwFlags,
//     LPTSTR pszPath
// );
	char strAppPath[MAX_PATH] = "";
	HRESULT ret = SHGetFolderPath(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, strAppPath);
	if (ret != S_OK) {
		return -1;
	}

	m_strHistFile.Format("%s\\%s", strAppPath, strFileName);
	FILE* fp = fopen(m_strHistFile, "rb");
	if (!fp) {
		return -1;
	}

	char buff[2048];
	m_histList.clear();
	while (fgets(buff, 2048, fp)) { //the '\n' is in the buff!
		cstring str = buff;
		str = regex_replace(str, regex("\r|\n"), "", match_default|format_perl);
		m_histList.push_back(str);
	}
	fclose(fp);
	m_histList = unique_ls(m_histList);
	if (m_histList.size()) {
		SetWindowText(m_histList.front());
		SetSel(0, GetLength());
	}
	fillListBox("");
	return 0;
}

void CEkbEdit::OnKillFocus(CWnd* pNewWnd) 
{
	CRichEditCtrl::OnKillFocus(pNewWnd);
	Invalidate(false);
	if ((m_simpleWnd && (CWnd*)m_simpleWnd == pNewWnd) || 
		(m_listBox && (CWnd*)m_listBox == pNewWnd)) {
		return;
	}

	if (m_simpleWnd) {
		//m_simpleWnd->ShowWindow(SW_HIDE);
	}
	
}

void CEkbEdit::OnSetFocus(CWnd* pOldWnd) 
{
	CRichEditCtrl::OnSetFocus(pOldWnd);
	//weVeMoved();
	//HideCaret();
	//Invalidate(false);
	if (m_simpleWnd && GetLength()) {
		m_simpleWnd->ShowWindow(SW_SHOWNA);
		m_listBox->SetCurSel(0);
	}
	
}

void CEkbEdit::weVeMoved()
{
	if (m_simpleWnd) {
		m_simpleWnd->weVeMoved();
	}
}

/////////////////////////////////////////////////////////////////////////////
// CEkbEdit message handlers
/////////////////////////////////////////////////////////////////////////////
// CEkbHistWnd

static ATOM RegisterClass(cstring str)
{
	static map<cstring, ATOM> name_class_map;
	
	if (name_class_map.find(str) != name_class_map.end()) {
		return name_class_map[str];
	}
	
	WNDCLASS     wndclass ;

	wndclass.style         = CS_HREDRAW | CS_VREDRAW ;
	wndclass.lpfnWndProc   = ::DefWindowProc ;
	wndclass.cbClsExtra    = 0 ;
	wndclass.cbWndExtra    = 0 ;
	wndclass.hInstance     = AfxGetInstanceHandle() ;
	wndclass.hIcon         = LoadIcon (NULL, IDI_APPLICATION) ;
	wndclass.hCursor       = LoadCursor (NULL, IDC_ARROW) ;
	wndclass.hbrBackground = (HBRUSH) GetStockObject (WHITE_BRUSH) ;
	wndclass.lpszMenuName  = NULL ;
	wndclass.lpszClassName = str.c_str();

	ATOM atom;
	if (!(atom = RegisterClass (&wndclass)))
	{
		fmt_messagebox("Failed to register class for %s", str.c_str());
		exit(-1);
	}

	name_class_map[str] = atom;
	return atom;
}

static HWND newWindow(cstring wc_name, HWND h_owner=NULL)
{
	RegisterClass(wc_name);
	return CreateWindow (wc_name.c_str(),                  // window class name
						 "",
						 WS_POPUP|WS_CLIPCHILDREN,
						 CW_USEDEFAULT,              // initial x position
						 CW_USEDEFAULT,              // initial y position
						 CW_USEDEFAULT,              // initial x size
						 CW_USEDEFAULT,              // initial y size
						 h_owner,                       // parent window handle
						 NULL,                       // window menu handle
						 AfxGetInstanceHandle(),                  // program instance handle
						 NULL) ;                     // creation parameters
}

LOGFONT getLogFont(CFont* font)
{
	LOGFONT lfont;
	font->GetLogFont(&lfont);
	return lfont;
}

CEkbHistWnd::CEkbHistWnd(CRichEditCtrl* master)
{
	m_master = master;
	
	HWND hwnd = newWindow ("CEkbHistWnd", getTopParentHwnd(master));

	SubclassWindow(hwnd);
	CFont* font = m_master->GetParentOwner()->GetFont();
	ModifyStyleEx(0, WS_EX_TOOLWINDOW);
	m_listBox = new CHListBox();
	
	m_listBox->Create(WS_VSCROLL|WS_HSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT, CRect(0, 0, 1, 1), this, 0);
	//m_listBox->ModifyStyleEx(0, WS_EX_RIGHT);
	m_listBox->SetFont(font);
	m_listBox->ShowWindow(SW_SHOWNA);
	m_listBox->UpdateWindow();

}

CEkbHistWnd::~CEkbHistWnd()
{
}


BEGIN_MESSAGE_MAP(CEkbHistWnd, CWnd)
	//{{AFX_MSG_MAP(CEkbHistWnd)
	ON_WM_SHOWWINDOW()
	ON_WM_PAINT()
	ON_WM_SIZE()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()


/////////////////////////////////////////////////////////////////////////////
// CEkbHistWnd message handlers

void CEkbHistWnd::OnShowWindow(BOOL bShow, UINT nStatus) 
{
	CWnd::OnShowWindow(bShow, nStatus);
	CRect rect;
	calcWindowRect(rect);
	SetWindowPos(&wndTop, rect.left, rect.top, rect.Width(), rect.Height(), SWP_NOACTIVATE);
}

void CEkbHistWnd::calcWindowRect(CRect& rect)
{
	m_master->GetWindowRect(&rect);
	CRect tmpRect = rect;
	int top = rect.bottom + 2;
	int left = rect.left;

	rect.OffsetRect(0, top-tmpRect.top);
	rect.bottom += rect.Height()*20;

	RECT waRect;
	SystemParametersInfo(SPI_GETWORKAREA, 0, &waRect, 0);


	if (rect.bottom > waRect.bottom) {
		int bottom = tmpRect.top - 2;
		int height = rect.Height();
		rect.bottom = bottom;
		rect.top =bottom - height;
	}
}

void CEkbHistWnd::hide()
{
	ShowWindow(SW_HIDE);
}

void CEkbHistWnd::show()
{
	ShowWindow(SW_SHOWNA);
}

void CEkbHistWnd::weVeMoved()
{
	CRect rect;
	calcWindowRect(rect);
	SetWindowPos(&wndTop, rect.left, rect.top, rect.Width(), rect.Height(), SWP_NOACTIVATE);
}

void CEkbHistWnd::OnPaint() 
{
	CPaintDC dc(this); // device context for painting
	CRect rect = ::GetClientRect(this);
	dc.FillSolidRect(&rect, RGB(0, 0, 0));
	
	
}
/////////////////////////////////////////////////////////////////////////////
// CBalloon

CBalloon* CBalloon::getInstance(CWnd *owner)
{
	static map<CWnd*, CBalloon*> owner_map;

	if (!owner_map[owner]) {
		owner_map[owner] = new CBalloon(owner);
		//owner_map[owner]->SetFont(owner->GetFont());
	}
	return owner_map[owner];
}

CBalloon::CBalloon(CWnd* owner)
{
	HWND hwnd = newWindow ("CBalloon", getTopParentHwnd(owner));

	SubclassWindow(hwnd);
	LOGFONT lf = getLogFont(owner->GetFont());
	m_font.CreateFontIndirect(&lf);
	ModifyStyleEx(0, WS_EX_TOOLWINDOW);
}

CBalloon::~CBalloon()
{
}


BEGIN_MESSAGE_MAP(CBalloon, CWnd)
	//{{AFX_MSG_MAP(CBalloon)
	ON_WM_SHOWWINDOW()
	ON_WM_PAINT()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()


/////////////////////////////////////////////////////////////////////////////
// CBalloon message handlers

void CEkbHistWnd::OnSize(UINT nType, int cx, int cy) 
{
	CWnd::OnSize(nType, cx, cy);
	
	CRect rect = ::GetClientRect(this);
	rect.DeflateRect(1, 1);

	m_listBox->MoveWindow(rect.left, rect.top, rect.Width(), rect.Height());
	
}

void CBalloon::OnShowWindow(BOOL bShow, UINT nStatus) 
{
	CWnd::OnShowWindow(bShow, nStatus);
}

static void debug_rect(const CRect& rect, const cstring& str)
{
	BHJDEBUG(" %s is %d %d %dx%d", str.c_str(), rect.left, rect.top, rect.Width(), rect.Height());
}

void CBalloon::showBalloon(CRect rect, const cstring& text)
{
	m_text = text;
	LONG cx = getTextWidth(text);

	//rect.InflateRect(m_border + (cx-rect.Width())/2, 0);
	rect.right = rect.left + cx;
	SetWindowPos(&wndTop, rect.left, rect.top, rect.Width(), rect.Height(), SWP_NOACTIVATE); 
	ShowWindow(SW_SHOWNA);
	UpdateWindow();
}

void CBalloon::OnPaint() 
{
	CPaintDC dc(this); // device context for painting

	CRect rect = ::GetClientRect(this);
	dc.FillSolidRect(&rect, GetSysColor(COLOR_HIGHLIGHT));
	dc.SetTextColor(RGB(255, 255, 255));
	dc.SelectObject(&m_font);
	dc.TextOut(0, 0, m_text);
}

/////////////////////////////////////////////////////////////////////////////
// CHListBox

CHListBox::CHListBox()
{
 width = 0;
}

CHListBox::~CHListBox()
{
}


BEGIN_MESSAGE_MAP(CHListBox, CListBox)
	//{{AFX_MSG_MAP(CHListBox)
	ON_CONTROL_REFLECT(LBN_SELCHANGE, OnSelchange)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CHListBox message handlers
void CHListBox::updateWidth(LPCTSTR s)
{
	CClientDC dc(this);
	CFont * f = CListBox::GetFont();
	dc.SelectObject(f);
	CSize sz = dc.GetTextExtent(s, _tcslen(s));
	sz.cx += 3 * ::GetSystemMetrics(SM_CXBORDER);
	if(sz.cx > width)
	{ /* extend */
		width = sz.cx;
		CListBox::SetHorizontalExtent(width);
	} /* extend */
}

int CHListBox::AddString(LPCTSTR s)
{
	int result = CListBox::AddString(s);
	if(result < 0)
		return result;
	updateWidth(s);
	return result;
}

int CHListBox::InsertString(int i, LPCTSTR s)
{
	int result = CListBox::InsertString(i, s);
	if(result < 0)
		return result;
	updateWidth(s);
	return result;
}

void CHListBox::ResetContent()
{
	CListBox::ResetContent();
	width = 0;
}

int CHListBox::DeleteString(int n)
{
	int result = CListBox::DeleteString(n);
	if(result < 0)
		return result;
	CClientDC dc(this);

	CFont * f = CListBox::GetFont();
	dc.SelectObject(f);

	width = 0;
	for(int i = 0; i < CListBox::GetCount(); i++)
	{ /* scan strings */
		CString s;
		CListBox::GetText(i, s);
		CSize sz = dc.GetTextExtent(s);
		sz.cx += 3 * ::GetSystemMetrics(SM_CXBORDER);
		if(sz.cx > width)
			width = sz.cx;
	} /* scan strings */
	CListBox::SetHorizontalExtent(width);
	return result;
}

int CHListBox::SetCurSel(int nSelect)
{
	int ret = CListBox::SetCurSel(nSelect);
	OnSelchange();

	return ret;
}

cstring CHListBox::getSelectedText()
{
	if (GetCurSel() < 0) {
		return "";
	}

	int i = GetCurSel();
	CString text;
	GetText(i, text);
	return text;
}

CRect CHListBox::GetItemRect(int idx)
{
	CRect rect;
	CListBox::GetItemRect(idx, &rect);
	CRect rect2 = ::GetClientRect(this);
	rect.right = rect.left+rect2.Width();
	return rect;
}

CRect CHListBox::getSelectedRect()
{
	if (GetCurSel()<0) {
		return ::GetWindowRect(this);
	}
	
	CRect rect = GetItemRect(GetCurSel());

	debug_rect(rect, "getSelectedRect, rect");
	CRect lbRect = ::GetWindowRect(this);
	debug_rect(lbRect, "getSelectedRect, lbRect");
	rect.OffsetRect(lbRect.left, lbRect.top);
	return rect;
}


void CHListBox::OnSelchange() 
{
		CClientDC dc(this);
	CFont * f = GetFont();
	dc.SelectObject(f);
	CSize sz = dc.GetTextExtent(getSelectedText());
	sz.cx += 3 * ::GetSystemMetrics(SM_CXBORDER);
	if (sz.cx > ::GetClientRect(this).Width()) {
		getBalloon(this)->showBalloon(getSelectedRect(), getSelectedText());
	} else {
		getBalloon(this)->ShowWindow(SW_HIDE);
	}
}

CSize CBalloon::getTextSize(cstring text)
{
	CClientDC dc(this);
	dc.SelectObject(&m_font);
	CSize sz = dc.GetTextExtent(text);
	return sz;
}

LONG CBalloon::getTextWidth(cstring str)
{
	CSize sz = getTextSize(str);
	return sz.cx;
}

CSize CEkbEdit::getTextSize(cstring text)
{
	CClientDC dc(this);
	dc.SelectObject(GetFont());
	CSize sz = dc.GetTextExtent(text);
	return sz;
}

LONG CEkbEdit::getTextWidth(cstring str)
{
	CSize sz = getTextSize(str);
	return sz.cx;
}

LONG CEkbEdit::getTextHeight(cstring str)
{
	CSize sz = getTextSize(str);
	return sz.cy;
}

cstring CEkbEdit::getSubText(int start, int end)
{

	//range of start: [ 0, GetLength()-1 ]
	//range of end:   [ 0, GetLength()   ]
	if (start > GetLength() - 1) {
		start = GetLength() - 1;
	}

	if (end > GetLength()) {
		end = GetLength();
	}

	start = start < 0 ? 0 : start;
	end = end < 0 ? 0 : end;
	
	
	if (start >= end) {
		return "";
	}

	return getText().substr(start, end-start);	
}

int CEkbEdit::OnCreate(LPCREATESTRUCT lpCreateStruct) 
{
	if (CRichEditCtrl::OnCreate(lpCreateStruct) == -1)
		return -1;
	
	return 0;
}

void CEkbEdit::OnWindowPosChanging(WINDOWPOS FAR* lpwndpos) 
{
	CRichEditCtrl::OnWindowPosChanging(lpwndpos);

	
}

HBRUSH CEkbEdit::CtlColor(CDC* pDC, UINT nCtlColor) 
{
	return NULL;
}

void CEkbEdit::OnHscroll() 
{
}

BOOL CEkbEdit::OnEnChange()
{
	// TODO:  If this is a RICHEDIT control, the control will not
	// send this notification unless you override the CRichEditCtrl::OnInitDialog()
	// function and call CRichEditCtrl().SetEventMask()
	// with the ENM_CHANGE flag ORed into the mask.

	// TODO:  Add your control notification handler code here
	OnChange();
	return false;
}
