#include<windows.h>
#include<winnt.h>
#include<dll_load.h>
#include"xmalloc.h"

extern CXMalloc g_mem_dll;

#pragma pack(push,1)

typedef struct {
	DWORD	dwPageRVA;
	DWORD	dwBlockSize;
} IMAGE_FIXUP_BLOCK, *PIMAGE_FIXUP_BLOCK;

typedef struct {
	WORD	offset:12;
	WORD	type:4;
} IMAGE_FIXUP_ENTRY, *PIMAGE_FIXUP_ENTRY;

#pragma pack(pop)

typedef struct __imageparameters {
	void	*pImageBase;
	TCHAR	svName[MAX_PATH];
	DWORD	dwFlags;
	int		nLockCount;
	struct __imageparameters *next;
} IMAGE_PARAMETERS;

typedef BOOL (WINAPI *DLLMAIN_T)(HMODULE hinstDLL, DWORD fdwReason, LPVOID lpvReserved); 


/*
void *xmemcpy(void *dst, const void *src, size_t len)
{
	size_t x;
	for(x=0;x<len;x++)
	{
		*(((char *)dst)+x)=*(((char *)src)+x);
	}
	return dst;
}
*/
#define xmemcpy memcpy

// Process-global variables

IMAGE_PARAMETERS *g_pImageParamHead=NULL;
CRITICAL_SECTION g_DLLCrit;

// Function implementations

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Initialization Routines                                                                                 //
/////////////////////////////////////////////////////////////////////////////////////////////////////////////

void InitializeDLLLoad(void)
{
	InitializeCriticalSection(&g_DLLCrit);
	g_pImageParamHead=NULL;
}

void KillDLLLoad(void)
{
	IMAGE_PARAMETERS *cur,*next;
	cur=g_pImageParamHead;
	while(cur!=NULL) {
		next=cur->next;
		free(cur);
		cur=next;
	}

	DeleteCriticalSection(&g_DLLCrit);
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Internal DLL list management                                                                            //
/////////////////////////////////////////////////////////////////////////////////////////////////////////////

// returns 0 if just ref count increment, 1 if this is a new addition, -1 on error
static int AddDLLReference(void *pImageBase, const TCHAR *svName, DWORD dwFlags)
{
	EnterCriticalSection(&g_DLLCrit);

	// Find DLL in list
	IMAGE_PARAMETERS *cur;
	for(cur=g_pImageParamHead;cur!=NULL;cur=cur->next) {
		if(cur->pImageBase==pImageBase) break;
	}
	
	if(cur!=NULL) {
		// increment dll count
		cur->nLockCount++;
		LeaveCriticalSection(&g_DLLCrit);
		return 0;
	} 

	// Add new dll to list
	cur=(IMAGE_PARAMETERS *)malloc(sizeof(IMAGE_PARAMETERS));
	if(cur==NULL) {
		LeaveCriticalSection(&g_DLLCrit);
		return -1;
	}
	
	cur->pImageBase=pImageBase;
	if(svName!=NULL) {
		wcsncpy(cur->svName,svName, MAX_PATH);
	} else cur->svName[0]='\0';
	cur->nLockCount=1;
	cur->dwFlags=dwFlags;
	cur->next=g_pImageParamHead;
	g_pImageParamHead=cur;
	
	LeaveCriticalSection(&g_DLLCrit);
	return 1;
}

// returns 0 if just a reference count dec, 1 if fully removed from list, -1 on error
static int RemoveDLLReference(void *pImageBase, TCHAR *svName, DWORD *pdwFlags)
{
	EnterCriticalSection(&g_DLLCrit);

	// Find DLL in list
	IMAGE_PARAMETERS *cur,*prev;
	prev=NULL;
	for(cur=g_pImageParamHead;cur!=NULL;cur=cur->next) {
		if(cur->pImageBase==pImageBase) break;
		prev=cur;
	}
	if(cur==NULL) {
		LeaveCriticalSection(&g_DLLCrit);
		return -1;
	}
	
	// decrement dll count
	cur->nLockCount--;
	// look up dll information 
	*pdwFlags=cur->dwFlags;
	wcsncpy(svName,cur->svName,MAX_PATH);

	// Remove if time to go
	if(cur->nLockCount==0) {
		if(prev==NULL) {
			g_pImageParamHead=g_pImageParamHead->next;
			free(cur);
		} else {
			prev->next=cur->next;
			free(cur);
		}

		LeaveCriticalSection(&g_DLLCrit);
		return 1;
	}
	
	LeaveCriticalSection(&g_DLLCrit);
	return 0;
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// GetDLLHandle()                                                                                          //
/////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Like GetModuleHandle(), returns null if the module is not loaded, returns a base address otherwise
HMODULE GetDLLHandle(const TCHAR *svName)
{
	if(svName==NULL) return NULL;

	EnterCriticalSection(&g_DLLCrit);

	// Find DLL in list
	IMAGE_PARAMETERS *cur;
	for(cur=g_pImageParamHead;cur!=NULL;cur=cur->next) {
		if(wcsicmp(cur->svName,svName)==0) break;
	}
	
	if(cur!=NULL) {
		LeaveCriticalSection(&g_DLLCrit);
		return (HMODULE) cur->pImageBase;
	} 

	LeaveCriticalSection(&g_DLLCrit);
	return NULL;	
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// GetDLLFileName()                                                                                          //
/////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Like GetModuleFileName(), returns 0 on failure
DWORD GetDLLFileName(HMODULE hModule, LPTSTR lpFileName, DWORD nSize)
{
	if(hModule==NULL || lpFileName==NULL || nSize==0) return 0;
	
	DWORD dwRet;
	if((dwRet=GetModuleFileName(hModule,lpFileName,nSize))!=0) return dwRet;

	EnterCriticalSection(&g_DLLCrit);

	// Find DLL in list
	IMAGE_PARAMETERS *cur;
	for(cur=g_pImageParamHead;cur!=NULL;cur=cur->next) {
		if(cur->pImageBase==hModule) break;
	}
	
	if(cur!=NULL) {
		LeaveCriticalSection(&g_DLLCrit);
		wcsncpy(lpFileName,cur->svName,nSize);
		return lstrlen(lpFileName);
	} 

	LeaveCriticalSection(&g_DLLCrit);
	return 0;	
}
					



/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// GetDLLProcAddress()                                                                                          //
/////////////////////////////////////////////////////////////////////////////////////////////////////////////



// Like GetProcAddress(), returns null if the procedure/ordinal is not there, otherwise returns function addr.
FARPROC GetDLLProcAddress(HMODULE hModule, LPCTSTR lpProcName)
{
	if(hModule==NULL) return NULL;


	char procname[512];
#ifdef UNICODE
	_snprintf(procname,512,"%S",lpProcName);
#else
	strncpy(procname,lpProcName,512);
	procname[511]='\0';
#endif
	
	// Get header
	
	PIMAGE_OPTIONAL_HEADER   poh;
    poh = (PIMAGE_OPTIONAL_HEADER)OPTHDROFFSET (hModule);
    
	// Get number of image directories in list
	
	int nDirCount;
	nDirCount=poh->NumberOfRvaAndSizes;
	if(nDirCount<16) return FALSE;

	// - Sift through export table -----------------------------------------------

	if(poh->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size==0) return NULL;

	// Good, we have an export table. Lets get it.
	
	PIMAGE_EXPORT_DIRECTORY ped;
	ped=(IMAGE_EXPORT_DIRECTORY *)RVATOVA(hModule,poh->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);	
	
	// Get ordinal of desired function
	
	int nOrdinal;
	
	if(HIWORD((DWORD)lpProcName)==0) {
		nOrdinal=(LOWORD((DWORD)lpProcName)) - ped->Base;
	} else {
	
		// Go through name table and find appropriate ordinal
		
		int i,count;
		DWORD *pdwNamePtr;
		WORD *pwOrdinalPtr;
		
		count=ped->NumberOfNames;
		pdwNamePtr=(DWORD *)RVATOVA(hModule,ped->AddressOfNames);
		pwOrdinalPtr=(WORD *)RVATOVA(hModule,ped->AddressOfNameOrdinals);
		
		for(i=0;i<count;i++) {
			// XXX should be a binary search, but, again, fuck it.
			
			char *svName;
			svName=(char *)RVATOVA(hModule,*pdwNamePtr);
			
			if(_stricmp(svName,procname)==0) {
				nOrdinal=*pwOrdinalPtr;
				break;
			}
			
			pdwNamePtr++;
			pwOrdinalPtr++;
		}
		if(i==count) return NULL;
	}
	
	// Look up RVA of this ordinal
	DWORD *pAddrTable;
	DWORD dwRVA;
	pAddrTable=(DWORD *)RVATOVA(hModule,ped->AddressOfFunctions);
	
	dwRVA=pAddrTable[nOrdinal];
	
	
	// Check if it's a forwarder, or a local addr
	// XXX  Should probably do this someday. Just don't define forwarders. You're
	// XXX  not loading kernel32.dll with this shit anyway.

	DWORD dwAddr;
	dwAddr=(DWORD) RVATOVA(hModule,dwRVA);

	return (FARPROC) dwAddr;
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SetDLLProcAddress()                                                                                          //
/////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Modifies an image. Must be loaded with RWX_PERMISSIONS, or have the export
// table writable
FARPROC SetDLLProcAddress(HMODULE hModule, LPCTSTR lpProcName, FARPROC fpAddr)
{
	if(hModule==NULL) return FALSE;
	
	char procname[512];
#ifdef UNICODE
	_snprintf(procname,512,"%S",lpProcName);
#else
	strncpy(procname,lpProcName,512);
	procname[511]='\0';
#endif

	// Get header
	
	PIMAGE_OPTIONAL_HEADER   poh;
    poh = (PIMAGE_OPTIONAL_HEADER)OPTHDROFFSET (hModule);
    
	// Get number of image directories in list
	
	int nDirCount;
	nDirCount=poh->NumberOfRvaAndSizes;
	if(nDirCount<16) return FALSE;

	// - Sift through export table -----------------------------------------------

	if(poh->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size==0) return FALSE;

	// Good, we have an export table. Lets get it.
	
	PIMAGE_EXPORT_DIRECTORY ped;
	ped=(IMAGE_EXPORT_DIRECTORY *)RVATOVA(hModule,poh->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);	
	
	// Get ordinal of desired function
	
	int nOrdinal;
	
	if(HIWORD((DWORD)lpProcName)==0) {
		nOrdinal=(LOWORD((DWORD)lpProcName)) - ped->Base;
	} else {
	
		// Go through name table and find appropriate ordinal
		
		int i,count;
		DWORD *pdwNamePtr;
		WORD *pwOrdinalPtr;
		
		count=ped->NumberOfNames;
		pdwNamePtr=(DWORD *)RVATOVA(hModule,ped->AddressOfNames);
		pwOrdinalPtr=(WORD *)RVATOVA(hModule,ped->AddressOfNameOrdinals);
		
		for(i=0;i<count;i++) {
			
			// XXX should be a binary search, but, again, fuck it.
			
			char *svName;
			svName=(char *)RVATOVA(hModule,*pdwNamePtr);
			
			if(strcmp(svName,procname)==0) {
				nOrdinal=*pwOrdinalPtr;
				break;
			}
			
			pdwNamePtr++;
			pwOrdinalPtr++;
		}
		if(i==count) return FALSE;
	}
	
	// Replace with different virtual address

	// Look up RVA of this ordinal and replace with RVA of other function
	DWORD *pAddrTable=(DWORD *)RVATOVA(hModule,ped->AddressOfFunctions);
	
	DWORD dwOldAddr=(DWORD) RVATOVA(hModule,(pAddrTable[nOrdinal]));
	pAddrTable[nOrdinal]=(DWORD) VATORVA(hModule,((DWORD)fpAddr));
	
	return (FARPROC) dwOldAddr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ResetDLLProcAddress()                                                                                   //
/////////////////////////////////////////////////////////////////////////////////////////////////////////////

BOOL ResetDLLProcAddress(HMODULE hModule, LPCTSTR lpProcName)
{
	TCHAR svName[MAX_PATH+1];
	if(GetDLLFileName(hModule,svName,MAX_PATH+1)) {
		// Load another copy of the DLL
		HMODULE hNewMod=LoadDLLEx(svName,NULL,FORCE_LOAD_NEW_IMAGE);
		if(hNewMod==NULL) return FALSE;
		DWORD dwAddr=(DWORD)GetDLLProcAddress(hNewMod,lpProcName);
		if(dwAddr==NULL) {
			FreeDLL(hNewMod);
			return FALSE;
		}

		DWORD dwNewAddr=(DWORD)RVATOVA(hModule,VATORVA(hNewMod,dwAddr));

		FreeDLL(hNewMod);

		SetDLLProcAddress(hModule,lpProcName,(FARPROC)dwNewAddr);

		return TRUE;
	}

	return FALSE;
}


				  
/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Internal DLL Loader code                                                                                //
/////////////////////////////////////////////////////////////////////////////////////////////////////////////


static BOOL RunDLLMain(void *pImageBase, DWORD dwImageSize, BOOL bDetach)
{
	// Get entry point from header
	PIMAGE_OPTIONAL_HEADER   poh;
	PIMAGE_FILE_HEADER pfh;
	DLLMAIN_T pDllMain;
	
    pfh = (PIMAGE_FILE_HEADER)PEFHDROFFSET(pImageBase);
	if((pfh->Characteristics & IMAGE_FILE_DLL)==0) return TRUE;
	
	poh = (PIMAGE_OPTIONAL_HEADER)OPTHDROFFSET (pImageBase);
	pDllMain=(DLLMAIN_T) RVATOVA(pImageBase, poh->AddressOfEntryPoint);

	// Call dllmain the right way

	BOOL bRet;
	if(bDetach) {
		bRet=pDllMain((HMODULE) pImageBase, DLL_PROCESS_DETACH, NULL);	
	} else {
		bRet=pDllMain((HMODULE) pImageBase, DLL_PROCESS_ATTACH, NULL);	
	}

	return bRet;
}

BOOL PrepareDLLImage(void *pMemoryImage, DWORD dwImageSize, BOOL bResolve, BOOL bRebind)
{
	// Get headers
	PIMAGE_OPTIONAL_HEADER   poh;
    PIMAGE_SECTION_HEADER    psh;
    poh = (PIMAGE_OPTIONAL_HEADER)OPTHDROFFSET (pMemoryImage);
    psh = (PIMAGE_SECTION_HEADER)SECHDROFFSET (pMemoryImage);

	// Get number of image directories in list
	int nDirCount;
	nDirCount=poh->NumberOfRvaAndSizes;
	if(nDirCount<16) return FALSE;
	
	// - Process import table -----------------------------------------------

	if(poh->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size!=0) {
		PIMAGE_IMPORT_DESCRIPTOR pid;
		pid=(IMAGE_IMPORT_DESCRIPTOR *)RVATOVA(pMemoryImage,poh->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
		
		// For all imported DLLs
		while(pid->OriginalFirstThunk!=0) {
			char *svDllName;
			svDllName=(char *) RVATOVA(pMemoryImage,pid->Name);
			
			// Map library into address space (could also use LoadDLL())
			HMODULE hDll;
#ifdef UNICODE
			wchar_t dllname[256];
			_snwprintf(dllname,256,L"%S",svDllName);
			hDll=GetModuleHandle(dllname);
			if(hDll==NULL) hDll=LoadLibrary(dllname);
#else
			hDll=GetModuleHandle(svDllName);
			if(hDll==NULL) hDll=LoadLibrary(svDllName);
#endif
	
			if(hDll==NULL) return FALSE;

			// Bind if not bound already
			if(pid->TimeDateStamp==0 || bRebind) {
				// Store DLL infoz
		
				pid->ForwarderChain=(DWORD)hDll;
				pid->TimeDateStamp=0xCDC31337; // This is bullshit cuz I don't want to call libc.
				
				// Fill in Import Address Table
				
				PIMAGE_THUNK_DATA ptd_in,ptd_out;
				ptd_in=(PIMAGE_THUNK_DATA) RVATOVA(pMemoryImage, pid->OriginalFirstThunk);
				ptd_out=(PIMAGE_THUNK_DATA) RVATOVA(pMemoryImage, pid->FirstThunk);
				
				while(ptd_in->u1.Function!=NULL) {
					FARPROC func;
					
					// Determine if ordinal or name pointer
					if(ptd_in->u1.Ordinal & 0x80000000) {
						// Ordinal
						func=GetProcAddress(hDll,MAKEINTRESOURCE(ptd_in->u1.Ordinal));
					} else {
						// Function name
						PIMAGE_IMPORT_BY_NAME pibn;
						pibn=(PIMAGE_IMPORT_BY_NAME) RVATOVA(pMemoryImage,ptd_in->u1.AddressOfData);
#ifdef UNICODE
						wchar_t procname[256];
						_snwprintf(procname,256,L"%S",(const char *)pibn->Name);
						func=GetProcAddress(hDll,procname);
#else
						func=GetProcAddress(hDll,(const char *)pibn->Name);
#endif
					}
					
					if(func==NULL) return FALSE;
					
					// Write address to appropriate location
					ptd_out->u1.Function = (PDWORD) func;
					
					ptd_in++;
					ptd_out++;
				}
			}

			pid++;
		}
	}

	// - Process relocation tables if necessary ----------------------------------

	// Calculate fixup delta
	DWORD delta;
	delta=(DWORD)pMemoryImage - (DWORD)poh->ImageBase;
	
	WORD *fixaddrhi=NULL;
	bool have_fixaddrhi=false;
	DWORD dwRelocSize=poh->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size;
	if((delta!=0) && (dwRelocSize!=0)) {
		PIMAGE_FIXUP_BLOCK pfb;
		pfb=(PIMAGE_FIXUP_BLOCK)RVATOVA(pMemoryImage,poh->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress);
		PIMAGE_FIXUP_BLOCK startpfb=pfb;
		
		// For each fixup block
		while(((DWORD)(((char *)pfb)-((char *)startpfb)))<dwRelocSize) {
			PIMAGE_FIXUP_ENTRY pfe;
			int i,count;

			count=0;
			if(pfb->dwBlockSize>0)
			{
				count=(pfb->dwBlockSize-sizeof(IMAGE_FIXUP_BLOCK))/sizeof(IMAGE_FIXUP_ENTRY);
				pfe=(PIMAGE_FIXUP_ENTRY)(((char *)pfb) + sizeof(IMAGE_FIXUP_BLOCK));
			}
			else
			{
				pfb=(PIMAGE_FIXUP_BLOCK)(((char *)pfb) + sizeof(IMAGE_FIXUP_BLOCK));		
				continue;
			}

			// For each fixup entry
			for(i=0;i<count;i++) {
				void *fixaddr;

				fixaddr=RVATOVA(pMemoryImage,pfb->dwPageRVA + pfe->offset);
				switch(pfe->type) {
#if defined(_X86_)
				case IMAGE_REL_BASED_ABSOLUTE:
					break;
				case IMAGE_REL_BASED_HIGH:
					*((WORD *)fixaddr) += HIWORD(delta);
					break;
				case IMAGE_REL_BASED_LOW:
					*((WORD *)fixaddr) += LOWORD(delta);
					break;
				case IMAGE_REL_BASED_HIGHLOW:
					*((DWORD *)fixaddr) += delta;
					break;
				case IMAGE_REL_BASED_HIGHADJ: // This one's really fucked up.
					{
						DWORD adjust					
						adjust=((*((WORD *)fixaddr)) << 16) | (*(WORD *)(pfe+1));
						adjust += delta;
						adjust += 0x00008000;
						*((WORD *)fixaddr) = HIWORD(adjust);
					}
					pfe++;
					break;
#elif defined(ARM)
				case IMAGE_REL_BASED_ABSOLUTE: 
					break;
				case IMAGE_REL_BASED_HIGH: 
					fixaddrhi=(WORD *)fixaddr;
					have_fixaddrhi=true;
					break;
				case IMAGE_REL_BASED_LOW:
					{
						DWORD out;
						if (have_fixaddrhi) 
						{
							out = (DWORD)(long)((*fixaddrhi) << 16) + *(WORD *)fixaddr + delta;
							*(WORD *)fixaddrhi = (WORD)((out + 0x8000) >> 16);
							have_fixaddrhi=false;
						}
						else
						{
							out = *(WORD *)fixaddr + delta;
						}
						*(WORD *)fixaddr = (WORD)(out & 0xffff);
					}
					break;
				case IMAGE_REL_BASED_HIGHLOW: 
					if ((DWORD)fixaddr & 0x3)
					{
						*(__unaligned DWORD *)fixaddr += delta;
					}
					else
					{
						(*(DWORD *)fixaddr)+=delta;
					}
					break;
				case IMAGE_REL_BASED_HIGHADJ: 
					*(WORD *)fixaddr += (WORD)( ((*(short *)(pfe+1))+delta+0x8000)>>16 );
					pfe++;
					break;
				case IMAGE_REL_BASED_MIPS_JMPADDR:
					{
						DWORD num=((*(DWORD *)fixaddr)&0x03ffffff)+(delta>>2);
						*(DWORD *)fixaddr=(*(DWORD *)fixaddr&0xfc000000)|(num & 0x03ffffff);
					}
					break;

#endif
				default:
					return FALSE;
				}
				
				pfe++;
			}

			pfb=(PIMAGE_FIXUP_BLOCK)((char *)pfb + pfb->dwBlockSize);
		}
	}

			

	return TRUE;
}

static BOOL ProtectDLLImage(void *pMemoryImage, BOOL bRWX)
{
	// Get Number of Sections
	PIMAGE_FILE_HEADER pfh;
	int nSectionCount;
	
	pfh=(PIMAGE_FILE_HEADER) PEFHDROFFSET(pMemoryImage);
	nSectionCount=pfh->NumberOfSections;

	// Get PE Header Length + Section Header Length
	PIMAGE_OPTIONAL_HEADER poh;
	DWORD hdrlen;

	poh=(PIMAGE_OPTIONAL_HEADER) OPTHDROFFSET(pMemoryImage);
	hdrlen=poh->SizeOfHeaders;

	// Protect sections one by one
	int i;
	PIMAGE_SECTION_HEADER psh;

	psh=(PIMAGE_SECTION_HEADER) SECHDROFFSET(pMemoryImage);
	for(i=0;i<nSectionCount;i++) {
		void *secMemAddr;
		int secLen;

		// Get Section Address
		secMemAddr  = (char *)RVATOVA(pMemoryImage, psh->VirtualAddress);
		secLen = psh->SizeOfRawData;
		
		// Parse Characteristics and protect memory appropriately
		DWORD newProtect=0,oldProtect;
		BOOL bWrite, bRead, bExec, bShared;
		
		bWrite  = (psh->Characteristics & IMAGE_SCN_MEM_WRITE)?TRUE:FALSE;
		bRead   = (psh->Characteristics & IMAGE_SCN_MEM_READ)?TRUE:FALSE;
		bExec   = (psh->Characteristics & IMAGE_SCN_MEM_EXECUTE)?TRUE:FALSE;
		bShared = (psh->Characteristics & IMAGE_SCN_MEM_SHARED)?TRUE:FALSE;
		
		if(bWrite && bRead && bExec && bShared) newProtect=PAGE_EXECUTE_READWRITE;
		else if(bWrite && bRead && bExec) newProtect=PAGE_EXECUTE_WRITECOPY;
		else if(bRead && bExec) newProtect=PAGE_EXECUTE_READ;
		else if(bExec) newProtect=PAGE_EXECUTE;
		else if(bWrite && bRead && bShared) newProtect=PAGE_READWRITE; 
		else if(bWrite && bRead) newProtect=PAGE_WRITECOPY;
		else if(bRead) newProtect=PAGE_READONLY;

		if(bRWX) newProtect=PAGE_WRITECOPY;

		if(psh->Characteristics & IMAGE_SCN_MEM_NOT_CACHED) newProtect |= PAGE_NOCACHE;

		if(newProtect==0) return FALSE;

		VirtualProtect(secMemAddr,secLen,newProtect,&oldProtect);
		
		psh++;
	}

	return TRUE;
}


BOOL MapDLLFromImage(void *pDLLFileImage, void *pMemoryImage)
{
	// Get Number of Sections
	PIMAGE_FILE_HEADER pfh;
	int nSectionCount;
	
	pfh=(PIMAGE_FILE_HEADER) PEFHDROFFSET(pDLLFileImage);
	nSectionCount=pfh->NumberOfSections;

	// Get PE Header Length + Section Header Length
	PIMAGE_OPTIONAL_HEADER poh;
	DWORD hdrlen;

	poh=(PIMAGE_OPTIONAL_HEADER) OPTHDROFFSET(pDLLFileImage);
	hdrlen=poh->SizeOfHeaders;

	// Copy PE Header + Section Headers
	xmemcpy(pMemoryImage,pDLLFileImage,hdrlen);
	
	// Copy Sections one by one
	int i;
	PIMAGE_SECTION_HEADER psh;

	psh=(PIMAGE_SECTION_HEADER) SECHDROFFSET(pDLLFileImage);
	for(i=0;i<nSectionCount;i++) {
		void *secMemAddr, *secFileAddr;
		int secLen;

		// Copy Section data
		secMemAddr  = (char *)pMemoryImage + psh->VirtualAddress;
		secFileAddr = (char *)pDLLFileImage + psh->PointerToRawData;
		secLen = psh->SizeOfRawData;
		
		memset(secMemAddr,0,psh->Misc.VirtualSize);
		memcpy(secMemAddr,secFileAddr,secLen);
		
		psh++;
	}
	


	return TRUE;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// LoadDLLFromImage()                                                                                      //
/////////////////////////////////////////////////////////////////////////////////////////////////////////////

HMODULE LoadDLLFromImage(void *pDLLFileImage, const TCHAR *svMappingName, DWORD dwFlags)
{
	// Examine DOS Header
	PIMAGE_DOS_HEADER doshead;
	doshead=(PIMAGE_DOS_HEADER) pDLLFileImage;
	if(doshead->e_magic!=IMAGE_DOS_SIGNATURE) return NULL;
	
	// Ensure our input is of good length
	if(svMappingName!=NULL) {
		if(wcslen(svMappingName) >= MAX_PATH) return NULL;
	}
	
	// Determine File Format
	if(*(DWORD *)NTSIGNATURE(pDLLFileImage) != IMAGE_NT_SIGNATURE) return NULL;
	
	
	// Get PE Header
	PIMAGE_FILE_HEADER pfh;
	pfh=(PIMAGE_FILE_HEADER) PEFHDROFFSET(pDLLFileImage);
	
	
	// Ensure proper machine type
	//if(pfh->Machine!=IMAGE_FILE_MACHINE_I386) return NULL;
	// XXX Verify Characteristics
	// XXX I don't bother to do this yet.

	
	// Get Section Count
	int nSectionCount;
	nSectionCount=pfh->NumberOfSections;

	
	// Get PE Optional Header
	PIMAGE_OPTIONAL_HEADER poh;
	poh=(PIMAGE_OPTIONAL_HEADER) OPTHDROFFSET(pDLLFileImage);


	// Ensure we are an executable image, not a rom image
	if(poh->Magic!=0x010B) return NULL;

	
	// Get preferred image base and image length
	void *pPreferredImageBase;
	DWORD dwImageSize;
	
	pPreferredImageBase=(void *)poh->ImageBase;
	dwImageSize=poh->SizeOfImage;

	// Get base address of virtual image
	void *pImageBase;
	HANDLE hmapping=NULL;
	
	pImageBase=GetDLLHandle(svMappingName);
	if(pImageBase==NULL) {				
		BOOL bCreated=FALSE;
		BOOL bRebased=FALSE;

		// If not mapped into this process, then we should map it now
		pImageBase=g_mem_dll.xmalloc(dwImageSize);
		if(!pImageBase)
		{
			return NULL;
		}

		bCreated=TRUE;	

		// Now map DLL from file image into appropriate memory image (if just created)
		// Also remap if DLL is being rebased as well (gotta fix relocations)
	
		if(bCreated || (pImageBase!=pPreferredImageBase)) {
			if(!MapDLLFromImage(pDLLFileImage,pImageBase)) {
				g_mem_dll.xfree(pImageBase,dwImageSize);
				return NULL;
			}
		}
		
		// Prepare DLL image (handle relocations/import/export/etc)
		
		if(!(dwFlags & LOAD_LIBRARY_AS_DATAFILE)) {
			if(!PrepareDLLImage(pImageBase, dwImageSize, (dwFlags & DONT_RESOLVE_DLL_REFERENCES)?FALSE:TRUE,(dwFlags & REBIND_IMAGE_IMPORTS)?TRUE:FALSE)) {
				g_mem_dll.xfree(pImageBase,dwImageSize);
				return NULL;
			}
			
			// Resolve DLL references
			
			if(!(dwFlags & DONT_RESOLVE_DLL_REFERENCES)) {
				BOOL bRet;
				bRet=RunDLLMain(pImageBase,dwImageSize,DLL_ATTACH);
				if(!bRet) {
					g_mem_dll.xfree(pImageBase,dwImageSize);
					return NULL;
				}
			}
			
			
			// Apply appropriate protections
//			if(!ProtectDLLImage(pImageBase, (dwFlags & RWX_PERMISSIONS)?TRUE:FALSE) ){
//				g_mem_dll.xfree(pImageBase,dwImageSize);
//				return NULL;
//			}
		}
	}
	

	// Add to DLL table/increase reference count
	
	int dllaction;
	dllaction=AddDLLReference(pImageBase,svMappingName,dwFlags);
	if(dllaction==-1) {
		if(hmapping!=NULL) {
			g_mem_dll.xfree(pImageBase,dwImageSize);
		}
		return NULL;
	}	

	return (HMODULE) pImageBase;	
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// LoadDLLEx()                                                                                             //
/////////////////////////////////////////////////////////////////////////////////////////////////////////////

HMODULE LoadDLLEx(LPCTSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
{
	const TCHAR *svFilePart;
	svFilePart=_tcsrchr(lpLibFileName,L'\\');
	if(svFilePart==NULL)
	{
		svFilePart=lpLibFileName;
	}
	else
	{
		svFilePart++;
	}

	// Open File
	HANDLE hfile=CreateFileForMapping(lpLibFileName,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
	if(hfile==INVALID_HANDLE_VALUE) return NULL;
	
	// Create a file mapping
	HANDLE hmapping;
	hmapping=CreateFileMapping(hfile, NULL, PAGE_READONLY, 0, 0, NULL);

	// Close file handle since we don't need it anymore
	CloseHandle(hfile);

	// Map file mapping object to memory image
	void *baseaddr;
	baseaddr=MapViewOfFile(hmapping,FILE_MAP_READ,0,0,0);
	if(baseaddr==NULL) {
		CloseHandle(hmapping);
		return NULL;
	}

	// Now pass off to LoadDLLFromImage
	HMODULE ret;
	if(dwFlags & FORCE_LOAD_NEW_IMAGE) {
		ret=LoadDLLFromImage(baseaddr, NULL, dwFlags & ~LOAD_WITH_ALTERED_SEARCH_PATH);
	} else {
		ret=LoadDLLFromImage(baseaddr, svFilePart, dwFlags & ~LOAD_WITH_ALTERED_SEARCH_PATH);
	}

		
	// Close file mapping
	UnmapViewOfFile(baseaddr);
	CloseHandle(hmapping);

	// Return base address as an instance handle
	return (HMODULE) ret;
}



/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// LoadDLL()                                                                                               //
/////////////////////////////////////////////////////////////////////////////////////////////////////////////



HMODULE LoadDLL(LPCTSTR lpLibFileName)
{
	return LoadDLLEx(lpLibFileName,NULL,0);
}




/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FreeDLL()                                                                                               //
/////////////////////////////////////////////////////////////////////////////////////////////////////////////


BOOL FreeDLL(HMODULE hLibModule)
{
	if(hLibModule==NULL) return FALSE;
	
	// Examine DOS Header
	PIMAGE_DOS_HEADER doshead;
	doshead=(PIMAGE_DOS_HEADER) hLibModule;
	if(doshead->e_magic!=IMAGE_DOS_SIGNATURE) return FALSE;
	
	// Determine File Format
	if(*(DWORD *)NTSIGNATURE(hLibModule) != IMAGE_NT_SIGNATURE) return FALSE;
		
	// Get PE Optional Header
	PIMAGE_OPTIONAL_HEADER poh;
	poh=(PIMAGE_OPTIONAL_HEADER) OPTHDROFFSET(hLibModule);
		
	// Ensure we are an executable image, not a rom image
	if(poh->Magic!=0x010B) return FALSE;
	
	// Get image length
	DWORD dwImageSize;
	dwImageSize=poh->SizeOfImage;

	// Get from DLL table/decrease reference count
	DWORD dwFlags;
	TCHAR svName[MAX_PATH];
	int dllaction;

	dllaction=RemoveDLLReference(hLibModule,svName,&dwFlags);
	if(dllaction==-1) return NULL;

	// Call DllMain if necessary
	BOOL bRet;
	bRet=TRUE;

	if(!(dwFlags & (LOAD_LIBRARY_AS_DATAFILE | DONT_RESOLVE_DLL_REFERENCES))) {
		if(dllaction) {
			RunDLLMain(hLibModule,dwImageSize,DLL_DETACH);
		}
	}
	g_mem_dll.xfree(hLibModule,dwImageSize);

	return bRet;
}