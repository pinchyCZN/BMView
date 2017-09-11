#define _WIN32_WINNT 0x400
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <conio.h>
#include <fcntl.h>
#include "resource.h"

HWND		hWindow;
HINSTANCE	ghInstance;

int _fseeki64(FILE *stream,__int64 offset,int origin);
__int64 _ftelli64(FILE *stream);
DWORD tick=0;
int screen_updated=TRUE;
int bytestep=1;
int bitmap=0;
int xdelta=640;
#define BUF_WIDTH 640
#define BUF_HEIGHT 480
int zoom=1;
int stretch=0;
enum {RGB888A=0,RGB888,BGR888A,BGR888,RGB555A,RGB8,BITMAP1,BITMAP2,RGBEND};
typedef struct {int type;char *name;}MODES;
MODES display_modes[]={
	{RGB888A,"RGB888A"},
	{RGB888,"RGB888"},
	{BGR888A,"BGR888A"},
	{BGR888,"BGR888"},
	{RGB555A,"RGB555A"},
	{RGB8,"RGB8"},
	{BITMAP1,"BITMAP1"},
	{BITMAP2,"BITMAP 2"},
	0};
int RGBmode=RGB8;
__int64 file_size=0,offset=0;
int BUF_SIZE=BUF_WIDTH*BUF_HEIGHT*3;
BYTE *buffer=0;
BYTE fname[MAX_PATH];
FILE *file=0;

#define TIME1 tick=GetTickCount();
#define TIME2 debug_printf("time=%u\n",GetTickCount()-tick);
#define ZOOM_IN_KEY 0xDD
#define ZOOM_OUT_KEY 0xDB
#define RGBMODE_KEY_UP '1'
#define RGBMODE_KEY_DOWN '2'

void debug_printf(char *fmt,...)
{
	va_list ap;
	char s[255];
	va_start(ap,fmt);
	_vsnprintf(s,sizeof(s),fmt,ap);
	OutputDebugString(s);
}
void open_console()
{
	BYTE Title[200]; 
	BYTE ClassName[200]; 
	LPTSTR  lpClassName=ClassName; 
	HANDLE hConWnd; 
	FILE *hf;
	static BYTE consolecreated=FALSE;
	static int hCrt=0;
	
	if(consolecreated==TRUE)
	{

		GetConsoleTitle(Title,sizeof(Title));
		hConWnd=FindWindow(NULL,Title);
		GetClassName(hConWnd,lpClassName,120);
		ShowWindow(hConWnd,SW_SHOW);
		SetForegroundWindow(hConWnd);
		hConWnd=GetStdHandle(STD_INPUT_HANDLE);
		FlushConsoleInputBuffer(hConWnd);
		return;
	}
	AllocConsole(); 
	hCrt=_open_osfhandle((long)GetStdHandle(STD_OUTPUT_HANDLE),_O_TEXT);

	fflush(stdin);
	hf=_fdopen(hCrt,"w"); 
	*stdout=*hf; 
	setvbuf(stdout,NULL,_IONBF,0);

	GetConsoleTitle(Title,sizeof(Title));
	hConWnd=FindWindow(NULL,Title);
	GetClassName(hConWnd,lpClassName,120);
	ShowWindow(hConWnd,SW_SHOW); 
	SetForegroundWindow(hConWnd);
	consolecreated=TRUE;
}

UINT CALLBACK OFNHookProc(HWND hDlg,UINT Msg,WPARAM wParam,LPARAM lParam)
{
	
	HWND hWnd;
	RECT rect;
	static int init_size=TRUE,init_details=TRUE;
	static int scroll_pos=0;
	static int last_selection=0;
	
	switch(Msg)
	{
	case WM_INITDIALOG:
		init_details=TRUE;
		SetForegroundWindow(hDlg);
		return 0; //0=dialog process msg,nonzero=ignore
	case WM_NOTIFY:
		PostMessage(hDlg,WM_APP + 1,0,0); 
		return 0;
	case WM_APP + 1:
		{ 
			int i;
			HWND const dlg     =GetParent(hDlg); 
			HWND defView =GetDlgItem(dlg,0x0461); 
			HWND list=GetDlgItem(defView,1);
			if(init_details)
			{
				SendMessage(defView,WM_COMMAND,28716,0); //details view
				ListView_EnsureVisible(list,last_selection,FALSE);
				ListView_SetItemState(list,last_selection,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
				SetFocus(list);
				init_details=FALSE;
			}
			if(ListView_GetItemCount(list)>0)
			{
				ListView_SetColumnWidth(list,0,LVSCW_AUTOSIZE);
				ListView_SetColumnWidth(list,1,LVSCW_AUTOSIZE);
				ListView_SetColumnWidth(list,2,LVSCW_AUTOSIZE);
				ListView_SetColumnWidth(list,3,LVSCW_AUTOSIZE);
			}
			i=ListView_GetNextItem(list,-1,LVNI_SELECTED);
			if(i>=0)
				last_selection=i;
			
			hWnd=GetDesktopWindow();
			if(init_size && GetWindowRect(hWnd,&rect)) //only do at start ,later resizing operations remain
			{
				SetWindowPos(dlg,HWND_TOP,0,0,(int)(rect.right*.75),(int)(rect.bottom*.75),0);
				init_size=FALSE;
			}
		} 
		return TRUE;
	case WM_DESTROY:
		return 0;
		break;
	default:  
		return 0;
	}
	
}



int OpenFileR(char *title,HWND hwnd,char fname[],int size)
{
	static TCHAR szFilter[]=TEXT("all files\0*.*;\0\0");
	static TCHAR szFileName[MAX_PATH],startpath[MAX_PATH];
	static TCHAR szTitleName[MAX_PATH];
	static OPENFILENAME ofn;
	memset(&ofn,0,sizeof(OPENFILENAME));
	memset(szFileName,0,sizeof(szFileName));
	
	ofn.hwndOwner		=hwnd;
	ofn.lStructSize     =sizeof(OPENFILENAME);
	ofn.lpstrFilter     =szFilter;
	ofn.lpstrFile       =szFileName;          // Set in Open and Close functions
	ofn.nMaxFile        =MAX_PATH;
	ofn.lpstrFileTitle  =szTitleName;          // Set in Open and Close functions
	ofn.nMaxFileTitle   =MAX_PATH;
	ofn.lpfnHook		=OFNHookProc;
	ofn.Flags			=OFN_ENABLEHOOK|OFN_EXPLORER|OFN_ENABLESIZING;
	ofn.lpstrTitle		=title;
	
	strcpy(startpath,".");
	ofn.lpstrInitialDir  =startpath;
	
	if(GetOpenFileName(&ofn)==0)
		return FALSE;
	else
		strncpy(fname,szFileName,size);
	return TRUE;
}

#define GRIPPIE_SQUARE_SIZE 15
static HANDLE grippy;
static HWND grip_hwnd=0;

int create_grippy(HWND hwnd)
{
	RECT client_rect;
	GetClientRect(hwnd,&client_rect);
	
	grip_hwnd=CreateWindow("Scrollbar",NULL,WS_CHILD|WS_VISIBLE|SBS_SIZEGRIP,
		client_rect.right-GRIPPIE_SQUARE_SIZE,
		client_rect.bottom-GRIPPIE_SQUARE_SIZE,
		GRIPPIE_SQUARE_SIZE,GRIPPIE_SQUARE_SIZE,
		hwnd,NULL,NULL,NULL);

	return 0;
}

int grippy_move(HWND hwnd)
{
	RECT client_rect;
	GetClientRect(hwnd,&client_rect);
	if(grip_hwnd!=0)
	{
		SetWindowPos(grip_hwnd,NULL,
			client_rect.right-GRIPPIE_SQUARE_SIZE,
			client_rect.bottom-GRIPPIE_SQUARE_SIZE,
			GRIPPIE_SQUARE_SIZE,GRIPPIE_SQUARE_SIZE,
			SWP_NOZORDER|SWP_SHOWWINDOW);
	}
	return 0;
}
int get_filename(char *path,char *fname,int size)
{
	char drive[4];
	char dir[255];
	char name[255];
	char ext[255];
	_splitpath(path,drive,dir,name,ext);
	_snprintf(fname,size,"%s%s",name,ext);
	return TRUE;
}


void set_pixel(BYTE *buf,int x,int y,BYTE R,BYTE G,BYTE B)
{
	int offset;
	if(x>=BUF_WIDTH)
		return;
	if(y>=BUF_HEIGHT)
		return;
	offset=x*3+y*BUF_WIDTH*3;
	if(offset<0)
		return;
	if((offset+2)>=BUF_SIZE)
		return;
	else{
		buf[offset]=B;
		buf[offset+1]=G;
		buf[offset+2]=R;
	}
}

void drawbuffer(FILE *f,__int64 offset,BYTE *buffer,int mode,int zoom,int bytestep,int xdelta)
{
	int i,j,x,y;
	BYTE R,G,B;
	BYTE data[12];
	__int64 fpos=offset;
	if(buffer==0)
		return;
	if(f==0)
		return;
	if(zoom<=0){
		OutputDebugString("zoom error\n");
		Beep(400,100);
		zoom=1;
	}
	_fseeki64(f,fpos,SEEK_SET);
	switch(mode){
	default:
		for(y=0;y<BUF_HEIGHT;y+=zoom){
			__int64 line=fpos;
			int shift=0;
			if(_fseeki64(f,fpos,SEEK_SET)!=0)
				break;
			fpos+=xdelta*bytestep;
			for(x=0;x<BUF_WIDTH;x+=zoom){
				int step_size=4;
				switch(mode){
				default:
				case BGR888A:
				case RGB888A:step_size=4;break;
				case BGR888:
				case RGB888:step_size=3;break;
				case RGB555A:step_size=2;break;
				case RGB8:step_size=1;break;
				}
				if(x>=(xdelta*zoom))
					break;
				if(shift==0){
					memset(data,0,sizeof(data));
					if(fread(data,1,sizeof(data),f)==0)
						break;
				}
				switch(mode){
				case RGB555A:
					B=(data[shift+0]<<0)&0xF8;
					G=(data[shift+0]<<5)|((data[shift+1]>>3)&0xF8);
					R=(data[shift+1]<<2)&0xF8;
					break;
				case RGB8:R=G=B=data[shift+0];break;
				case BGR888:
				case BGR888A:
					B=data[shift+0];
					G=data[shift+1];
					R=data[shift+2];
					break;
				default:
					R=data[shift+0];
					G=data[shift+1];
					B=data[shift+2];
					break;
				}
				if((bytestep>0) && (bytestep<=4) && mode==RGB8)
					shift+=bytestep;
				else
					shift+=step_size;
				shift%=sizeof(data);
				if(zoom>1){
					for(i=0;i<zoom;i++){
						for(j=0;j<zoom;j++){
							set_pixel(buffer,x+i,BUF_HEIGHT-1-y-j,R,G,B);
						}
					}
				}
				else{
					set_pixel(buffer,x,BUF_HEIGHT-1-y,R,G,B);
				}
			}
		}
		break;
	case BITMAP2:
		if(_fseeki64(f,fpos,SEEK_SET)!=0)
			break;
		for(y=0;y<BUF_HEIGHT;y+=xdelta*zoom){
			for(x=0;x<BUF_WIDTH;x+=zoom*8){
				int z;
				for(z=0;z<xdelta;z++){
					BYTE bit;
					if((z*zoom+y)>=BUF_HEIGHT)
						break;
					if(fread(data,1,1,file)==0)
						break;
					for(bit=0;bit<8;bit++){
						if(data[0]&(0x80>>bit))
							R=G=B=0x7F;
						else
							R=G=B=0;
						for(i=0;i<zoom;i++){
							for(j=0;j<zoom;j++){
								set_pixel(buffer,x+i+bit*zoom,BUF_HEIGHT-1-y-j-z*zoom,R,G,B);
							}
						}
					}
				}

			}
		}
		break;
	case BITMAP1:
		for(y=0;y<BUF_HEIGHT;y+=zoom){
			__int64 line=fpos;
			if(_fseeki64(f,fpos,SEEK_SET)!=0)
				break;
			fpos+=xdelta*bytestep;
			for(x=0;x<BUF_WIDTH;x+=zoom*8){
				BYTE bit;
				if(x>=(xdelta*zoom))
					break;
				if(fread(data,1,1,file)==0)
					break;
				for(bit=0;bit<8;bit++){
					if(data[0]&(0x80>>bit))
						R=G=B=0x7F;
					else
						R=G=B=0;
					for(i=0;i<zoom;i++){
						for(j=0;j<zoom;j++){
							set_pixel(buffer,x+i+bit*zoom,BUF_HEIGHT-1-y-j,R,G,B);
						}
					}
				}

			}
		}
		break;
	}
}
void set_window_title(HWND hwnd,char fname[],unsigned __int64 fsize)
{
	char str[MAX_PATH],f[80];
	if(fname[0]!=0){
		get_filename(fname,f,sizeof(f));
		_snprintf(str,sizeof(str),"%s %08I64X",f,fsize);
		SetWindowText(hwnd,str);
	}
	else
		SetWindowText(hwnd,"BMVIEW");
}
void display_help(HWND hwnd)
{
	MessageBox(hwnd,"< > xdelta\r\n"
		"+ - bytestep\r\n"
		"[] zoom\r\n"
		"tab=stretch to dialog\r\n"
		"F5=open file\r\n"
		"1/2/right click=select RGB mode\r\n"
		"space=clear buffer\r\n"
		"Q/W=select next/prev file\r\n"
		"Z=reset xdelta,bytestep,zoom\r\n"
		,"HELP",MB_OK);
}
void set_info(HWND hwnd,int ctrl)
{
	char str[80];
	_snprintf(str,sizeof(str),"offset=%08I64X step=%i xdelta=%i zoom=%i %s %s",offset,bytestep,xdelta,
		zoom,stretch?"stretch":"",display_modes[RGBmode].name);
	SetDlgItemText(hwnd,ctrl,str);
}
int set_current_dir(char *fname)
{
	char path[_MAX_PATH]={0};
	char drive[_MAX_DRIVE]={0};
	char dir[_MAX_DIR]={0};
	_splitpath(fname,drive,dir,0,0);
	_snprintf(path,sizeof(path),"%s%s",drive,dir);
	return SetCurrentDirectory(path);
}
int set_bytestep(int mode)
{
	switch(mode){
	case BGR888A:
	case RGB888A:return 4;
	case BGR888:
	case RGB888:return 3;
	case RGB555A:return 2;
	default:return 1;
	}
}
int find_next_file(char *fname,int size,int dir)
{
	HANDLE hfind;
	WIN32_FIND_DATA fd;
	int result=FALSE;
	static int index=-1;
	int current=0;
	if(index==-1){index=0;dir=0;}
	hfind=FindFirstFile("*.*",&fd);
	if(hfind!=INVALID_HANDLE_VALUE){
		while(FindNextFile(hfind,&fd)!=0){
			if(!(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)){
				if((index+dir)==current){
					strncpy(fname,fd.cFileName,size);
					result=TRUE;
					index=current;
					break;
				}
				current++;
			}
		}
		FindClose(hfind);
	}
	return result;
}
void file_error(HWND hwnd,char *fname)
{
	char str[MAX_PATH];
	char msg[MAX_PATH+40];
	str[0]=0;
	get_filename(fname,str,sizeof(str));
	_snprintf(msg,sizeof(msg),"Cant open file:\r\n%s",str);
	MessageBox(hwnd,msg,"FILE ERROR",MB_OK);
}

LRESULT CALLBACK request_value(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
	static int *val=0;
	switch(msg)
	{
	case WM_INITDIALOG:
		{
			RECT rect;
			val=(int*)lParam;
			SendDlgItemMessage(hwnd,IDC_EDIT1,EM_LIMITTEXT,20,0);
			GetWindowRect(hWindow,&rect);
			SetWindowPos(hwnd,NULL,(rect.right+rect.left)/2,(rect.top+rect.bottom)/2,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_SHOWWINDOW);
		}
		break;
	case WM_CLOSE:
	case WM_QUIT:
		EndDialog(hwnd,0);
		break;
	case WM_COMMAND:
		switch (LOWORD(wParam)){
		case IDOK:
			if(val){
				char str[40]={0};
				int i;
				GetDlgItemText(hwnd,IDC_EDIT1,str,sizeof(str));
				i=strtoul(str,NULL,0);
				if(i!=0)
					*val=i;
			}
			EndDialog(hwnd,0);
			break;
		case IDCANCEL:
			EndDialog(hwnd,0);
			break;
		}
	}
	return 0;
}
LRESULT CALLBACK MainDlg(HWND hwnd,UINT message,WPARAM wParam,LPARAM lParam)
{
	HDC hdc,hdcMem;
	PAINTSTRUCT ps;
	BITMAPINFO bmi;
	static RECT client_rect;
	int i;

#ifdef _DEBUG
//	if(message!=0x200&&message!=0x84&&message!=0x20&&message!=WM_ENTERIDLE)
//		debug_printf("message=%08X wParam=%08X lParam=%08X\n",message,wParam,lParam);
#endif	
	switch(message)
	{
	case WM_INITDIALOG:
		for(i=0;i<100;i++){
			if(display_modes[i].name==0)
				break;
			SendDlgItemMessage(hwnd,IDC_RGBMODE,CB_ADDSTRING,0,(LPARAM)display_modes[i].name);
			SendDlgItemMessage(hwnd,IDC_RGBMODE,CB_SETITEMDATA,0,display_modes[i].type);
			if(display_modes[i].type==RGBmode)
				SendDlgItemMessage(hwnd,IDC_RGBMODE,CB_SETCURSEL,i,0);
		}
		create_grippy(hwnd);
		BringWindowToTop(hwnd);
		GetClientRect(hwnd,&client_rect);
		break;
	case WM_SIZE:
		client_rect.right=LOWORD(lParam);
		client_rect.bottom=HIWORD(lParam);
		grippy_move(hwnd);
		InvalidateRect(hwnd,NULL,TRUE);
		break;
		
	case WM_COMMAND:

		switch(LOWORD(wParam))
		{
		case WM_DESTROY:
		#ifndef _DEBUG
			if(MessageBox(hwnd,"Sure you want to quit?","QUIT",MB_OKCANCEL)!=IDOK)
				break;
		#endif
			PostQuitMessage(0);
			break;
		}
		break;
		
	case WM_PAINT:
//		TIME1
		set_info(hwnd,IDC_INFO);
		hdc=BeginPaint(hwnd,&ps);
		memset(&bmi,0,sizeof(BITMAPINFO));
		bmi.bmiHeader.biBitCount=24;
		bmi.bmiHeader.biWidth=BUF_WIDTH;
		bmi.bmiHeader.biHeight=BUF_HEIGHT;
		bmi.bmiHeader.biPlanes=1;
		bmi.bmiHeader.biSize=40;
		if(stretch)
			StretchDIBits(hdc,0,30,client_rect.right-20,client_rect.bottom,0,0,BUF_WIDTH,BUF_HEIGHT,buffer,&bmi,DIB_RGB_COLORS,SRCCOPY);
		else
			SetDIBitsToDevice(hdc,0,30,BUF_WIDTH,BUF_HEIGHT,0,0,0,BUF_WIDTH,buffer,&bmi,DIB_RGB_COLORS);
		EndPaint(hwnd,&ps);
		screen_updated=TRUE;
//		TIME2
		break;
	case WM_CLOSE:
	case WM_QUIT:
		PostQuitMessage(0);
		break;
	case WM_DROPFILES:
		DragQueryFile((HDROP)wParam,0,fname,sizeof(fname));
		DragFinish((HDROP)wParam);
		if(GetFileAttributes(fname)!=FILE_ATTRIBUTE_DIRECTORY)
			SendMessage(hwnd,WM_KEYDOWN,0xDEADBEEF,0);
		break;
	case WM_LBUTTONDBLCLK:
	case WM_LBUTTONDOWN:
		break;
	case WM_RBUTTONDBLCLK:
	case WM_RBUTTONDOWN:
		memset(buffer,0,BUF_SIZE);
		SendMessage(hwnd,WM_KEYDOWN,RGBMODE_KEY_DOWN,0);
		break;
	case WM_MOUSEWHEEL:
		if(wParam&0x80000000)
			SendMessage(hwnd,WM_KEYDOWN,VK_NEXT,0);
		else
			SendMessage(hwnd,WM_KEYDOWN,VK_PRIOR,0);
		break;
	case WM_KEYUP:
		memset(buffer,0,BUF_SIZE);
		drawbuffer(file,offset,buffer,RGBmode,zoom,bytestep,xdelta);
		InvalidateRect(hwnd,NULL,TRUE);
		break;
	case WM_KEYDOWN:
#ifdef _DEBUG
//		debug_printf("message=%08X wParam=%08X lParam=%08X\n",message,wParam,lParam);
#endif
		switch(wParam)
		{
		case 'Z':
			xdelta=640;
			bytestep=1;
			zoom=1;
			break;
		case 'W':
			if(find_next_file(fname,sizeof(fname),1))
				SendMessage(hwnd,WM_KEYDOWN,0xDEADBEEF,0);
			break;
		case 'Q':
			if(find_next_file(fname,sizeof(fname),-1))
				SendMessage(hwnd,WM_KEYDOWN,0xDEADBEEF,0);
			break;
		case RGBMODE_KEY_UP:
			RGBmode++;
			if(RGBmode>=RGBEND)
				RGBmode=0;
			bytestep=set_bytestep(RGBmode);
			break;
		case RGBMODE_KEY_DOWN:
			RGBmode--;
			if(RGBmode<0)
				RGBmode=RGBEND-1;
			bytestep=set_bytestep(RGBmode);
			break;
		case VK_SPACE:
			memset(buffer,0,BUF_SIZE);
			break;
		case VK_TAB:
			stretch^=1;
			break;
		case VK_F1:
			display_help(hwnd);
			break;
		case VK_F2:
		case VK_RETURN:
			DialogBoxParam(ghInstance,MAKEINTRESOURCE(IDD_DIALOG2),hwnd,request_value,(LPARAM)&xdelta);
			break;
		case VK_F5:
			if(!OpenFileR("open raw image file",hwnd,fname,sizeof(fname)))
				break;
		case 0xDEADBEEF:
			if(file!=0){
				fclose(file);
				file=0;
			}
			file=fopen(fname,"rb");
			if(file==0)
				file_error(hwnd,fname);
			else{
				_fseeki64(file,0,SEEK_END);
				file_size=_ftelli64(file);
				_fseeki64(file,0,SEEK_SET);
				set_window_title(hWindow,fname,file_size);
				memset(buffer,0,BUF_SIZE);
				set_current_dir(fname);
			}
			break;
		case VK_DOWN:
			if(GetKeyState(VK_SHIFT)&0x8000)
				offset+=bytestep*xdelta*10;
			else
				offset+=bytestep*xdelta;
			break;
		case VK_UP:
			if(GetKeyState(VK_SHIFT)&0x8000)
				offset-=bytestep*xdelta*10;
			else
				offset-=bytestep*xdelta;
			break;
		case VK_LEFT:
			if(GetKeyState(VK_CONTROL)&0x8000)
				offset--;
			else if(GetKeyState(VK_SHIFT)&0x8000)
				offset-=(bytestep*xdelta/2/zoom)&-bytestep;
			else
				offset-=bytestep;
			break;
		case VK_RIGHT:
			if(GetKeyState(VK_CONTROL)&0x8000)
				offset++;
			else if(GetKeyState(VK_SHIFT)&0x8000)
				offset+=(bytestep*xdelta/2/zoom)&-bytestep;
			else
				offset+=bytestep;
			break;
		case VK_ADD:
			bytestep+=1;
			break;
		case VK_SUBTRACT:
			bytestep-=1;
			break;
		case VK_NEXT: //page key
			if(GetKeyState(VK_CONTROL)&0x8000)
				offset+=xdelta*bytestep*(BUF_HEIGHT/zoom)/2;
			else if(GetKeyState(VK_SHIFT)&0x8000)
				offset+=file_size/10;
			else
				offset+=xdelta*bytestep*(BUF_HEIGHT/zoom);
			break;
		case VK_PRIOR: //page key
			if(GetKeyState(VK_CONTROL)&0x8000)
				offset-=xdelta*bytestep*(BUF_HEIGHT/zoom)/2;
			else if(GetKeyState(VK_SHIFT)&0x8000)
				offset-=file_size/10;
			else
				offset-=xdelta*bytestep*(BUF_HEIGHT/zoom);
			break;
		case VK_HOME:
			offset=0;
			break;
		case VK_END:
			offset=file_size;
			break;
		case ZOOM_IN_KEY: //[
			if(GetKeyState(VK_SHIFT)&0x8000){
				if(GetKeyState(VK_CONTROL)&0x8000)
					zoom+=50;
				else
					zoom+=5;
			}
			else
				zoom++;
			break;
		case ZOOM_OUT_KEY: //]
			if(GetKeyState(VK_SHIFT)&0x8000){
				if(GetKeyState(VK_CONTROL)&0x8000)
					zoom-=50;
				else
					zoom-=5;
			}
			else
				zoom--;
			if(zoom<=0)
				zoom=1;
			break;
		case 0xBE:  //>
			if(GetKeyState(VK_SHIFT)&0x8000){
				if(GetKeyState(VK_CONTROL)&0x8000)
					xdelta+=100;
				else
					xdelta+=10;
			}
			else
				xdelta+=1;
			break;
		case 0xBC:  //<
			if(GetKeyState(VK_SHIFT)&0x8000){
				if(GetKeyState(VK_CONTROL)&0x8000)
					xdelta-=100;
				else
					xdelta-=10;
			}
			else
				xdelta-=1;
			break;
		case VK_ESCAPE:
			if(MessageBox(hwnd,"Sure you want to quit?","QUIT",MB_OKCANCEL)==IDOK)
				PostQuitMessage(0);
			break;
		}
		if(offset<0)
			offset=0;
		if(offset>=file_size)
			offset=file_size;
		if(xdelta<=0)
			xdelta=1;
		if(bytestep<1) bytestep=4;
		if(bytestep>4) bytestep=1;
		drawbuffer(file,offset,buffer,RGBmode,zoom,bytestep,xdelta);
		InvalidateRect(hwnd,NULL,TRUE);   // force redraw
		break;
	}
	return 0;
}


int WINAPI WinMain(HINSTANCE hInstance,HINSTANCE hPrevInstance,PSTR szCmdLine,int iCmdShow)
{
	MSG msg;
	ghInstance=hInstance;
	
	hWindow=CreateDialog(ghInstance,MAKEINTRESOURCE(IDD_DIALOG1),NULL,MainDlg);
	if(!hWindow){
		
		MessageBox(NULL,"Could not create main dialog","ERROR",MB_ICONERROR | MB_OK);
		return 0;
	}

	buffer=malloc(BUF_SIZE);
	if(buffer==0){
		MessageBox(hWindow,"cant allocate image buffer!","BMVIEW",MB_ICONERROR);
		return -1;
	}
#ifdef _DEBUG
//	open_console();
#endif
	ShowWindow(hWindow,iCmdShow);
	UpdateWindow(hWindow);

	memset(buffer,0,BUF_SIZE);
	if(szCmdLine!=0 && szCmdLine[0]!=0){
		strncpy(fname,szCmdLine,sizeof(fname));
		file=fopen(fname,"rb");
		if(file==0)
			file_error(hWindow,fname);
		else{
			_fseeki64(file,0,SEEK_END);
			file_size=_ftelli64(file);
			_fseeki64(file,0,SEEK_SET);
			set_window_title(hWindow,fname,file_size);
			SendMessage(hWindow,WM_KEYDOWN,0,0);
		}
	}
	else
		fname[0]=0;

	while(GetMessage(&msg,NULL,0,0))
	{
		if(!IsDialogMessage(hWindow,&msg)){		// Translate messages for the dialog
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else{
		//	debug_printf("msg.message=%08X msg.lparam=%08X msg.wparam=%08X\n",msg.message,msg.lParam,msg.wParam);
		//	DispatchMessage(&msg);
			//if(msg.message == WM_KEYDOWN && msg.wParam!=VK_ESCAPE){
			if((msg.message == WM_KEYDOWN && msg.wParam!=VK_ESCAPE && msg.wParam!=VK_SHIFT && msg.wParam!=VK_CONTROL)
				|| (msg.message==WM_KEYUP && msg.wParam!=VK_ESCAPE && msg.wParam!=VK_SHIFT && msg.wParam!=VK_CONTROL)){
				static DWORD time=0;
				//if((GetTickCount()-time)>100){
				{
					if(screen_updated){					
						screen_updated=FALSE;
						SendMessage(hWindow,msg.message,msg.wParam,msg.lParam);
						time=GetTickCount();
					}
				}
			}
		}
	}
	free(buffer);
	if(file!=0)
		fclose(file);
	return msg.wParam;
}