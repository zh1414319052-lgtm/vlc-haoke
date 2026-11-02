/* Modified by Haoke, 2026-11-02: independent Windows edition UI, localization and lifecycle integration. Original authors and license retained. */
/*****************************************************************************
 * winvlc.c: the Windows VLC media player
 *****************************************************************************
 * Copyright (C) 1998-2011 the VideoLAN team
 *
 * Authors: Vincent Seguin <seguin@via.ecp.fr>
 *          Samuel Hocevar <sam@zoy.org>
 *          Gildas Bazin <gbazin@videolan.org>
 *          Derk-Jan Hartman <hartman at videolan dot org>
 *          Lots of other people, see the libvlc AUTHORS file
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifndef UNICODE
#define UNICODE
#endif

#include <vlc/vlc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <shellapi.h>

#ifndef _WIN32_IE
#  define  _WIN32_IE 0x501
#endif
#include <fcntl.h>
#include <io.h>
#include <shlobj.h>
#include "../../haoke/win_language.h"
#define HeapEnableTerminationOnCorruption (HEAP_INFORMATION_CLASS)1

/* Haoke QA: use only the first Windows UI preference. This is process-local;
 * never change the user's system language or the official VLC registry. */
static const char *HaokeLanguage(const wchar_t *explicit_language)
{
    ULONG count = 0, size = 0;
    wchar_t *languages = NULL;
    typedef BOOL (WINAPI *GetLanguages)(DWORD, PULONG, PZZWSTR, PULONG);
    GetLanguages get_languages = (GetLanguages)GetProcAddress(
        GetModuleHandleW(L"kernel32.dll"), "GetUserPreferredUILanguages");
    if (get_languages && get_languages(MUI_LANGUAGE_NAME, &count, NULL, &size)
        && size > 1 && size < 65536)
    {
        languages = calloc(size, sizeof(*languages));
        if (languages && !get_languages(MUI_LANGUAGE_NAME, &count, languages, &size))
        {
            free(languages);
            languages = NULL;
        }
    }
    const char *language = haoke_select_language(explicit_language, languages);
    free(languages);
    return language;
}

static void HaokeSetLanguage(const char *language)
{
    /* gettext consults CRT state; Qt and child libraries may use Win32 state.
     * Keep both in sync and override inherited colon-separated candidates. */
    const char *names[] = { "LANGUAGE", "LANG", "LC_ALL", "LC_MESSAGES" };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
    {
        _putenv_s(names[i], language);
        SetEnvironmentVariableA(names[i], language);
    }
}

static void HaokeStartError(const char *language, BOOL language_error)
{
    BOOL chinese = !strcmp(language, "zh_CN");
    const wchar_t *message = language_error
        ? (chinese ? L"语言参数缺少值。请使用 --language auto、--language zh_CN 或 --language=en_US。"
                   : L"Missing language value. Use --language auto, --language zh_CN or --language=en_US.")
        : (chinese ? L"全能影音播放器无法启动。请检查启动参数，并将便携包完整解压后重试。"
                   : L"VLC media player could not start. Check the command-line options and extract the entire portable package before retrying.");
    MessageBoxW(NULL, message,
        chinese ? L"全能影音播放器" : L"VLC media player", MB_OK | MB_ICONERROR);
}

#ifdef HAVE_BREAKPAD
void CheckCrashDump( const wchar_t* crashdump_path );
void* InstallCrashHandler( const wchar_t* crashdump_path );
void ReleaseCrashHandler( void* handler );
#endif

static char *FromWide (const wchar_t *wide)
{
    size_t len;
    len = WideCharToMultiByte (CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);

    char *out = (char *)malloc (len);
    if (out)
        WideCharToMultiByte (CP_UTF8, 0, wide, -1, out, len, NULL, NULL);
    return out;
}

#if (_WIN32_WINNT < _WIN32_WINNT_WIN8)
static BOOL SetDefaultDllDirectories_(DWORD flags)
{
    HMODULE h = GetModuleHandle(TEXT("kernel32.dll"));
    if (h == NULL)
        return FALSE;

    BOOL (WINAPI * SetDefaultDllDirectoriesReal)(DWORD);

    SetDefaultDllDirectoriesReal = (BOOL (WINAPI *)(DWORD))
                                    GetProcAddress(h, "SetDefaultDllDirectories");
    if (SetDefaultDllDirectoriesReal == NULL)
        return FALSE;

    return SetDefaultDllDirectoriesReal(flags);
}
# define SetDefaultDllDirectories SetDefaultDllDirectories_

#endif

static void PrioritizeSystem32(void)
{
#ifndef HAVE_PROCESS_MITIGATION_IMAGE_LOAD_POLICY
    typedef struct _PROCESS_MITIGATION_IMAGE_LOAD_POLICY {
      union {
        DWORD  Flags;
        struct {
          DWORD NoRemoteImages  :1;
          DWORD NoLowMandatoryLabelImages  :1;
          DWORD PreferSystem32Images  :1;
          DWORD ReservedFlags  :29;
        };
      };
    } PROCESS_MITIGATION_IMAGE_LOAD_POLICY;
#endif
#if _WIN32_WINNT < _WIN32_WINNT_WIN8
    BOOL WINAPI (*SetProcessMitigationPolicy)(PROCESS_MITIGATION_POLICY, PVOID, SIZE_T);
    HINSTANCE h_Kernel32 = GetModuleHandle(TEXT("kernel32.dll"));
    if ( !h_Kernel32 )
        return;
    SetProcessMitigationPolicy = (BOOL (WINAPI *)(PROCESS_MITIGATION_POLICY, PVOID, SIZE_T))
                                   GetProcAddress(h_Kernel32, "SetProcessMitigationPolicy");
    if (SetProcessMitigationPolicy == NULL)
        return;
#endif
    PROCESS_MITIGATION_IMAGE_LOAD_POLICY m = { .Flags = 0 };
    m.PreferSystem32Images = 1;
    SetProcessMitigationPolicy( 10 /* ProcessImageLoadPolicy */, &m, sizeof( m ) );
}

/*
 * Export WinMain to force GNU ld to generate a .reloc section
 */
__declspec(dllexport)
int WINAPI WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance,
                    LPSTR lpCmdLine,
                    int nCmdShow )
{
    int argc;

    /* VLC does not change the thread locale, so gettext/libintil will use the
     * user default locale as reference. */
    /* gettext versions 0.18-0.18.1 will use the Windows Vista locale name
     * if the GETTEXT_MUI environment variable is set. If not set or if running
     * on Windows 2000/XP/2003 an hard-coded language ID list is used. This
     * putenv() call may become redundant with later versions of gettext. */
    putenv("GETTEXT_MUI=1");
#ifdef TOP_BUILDDIR
    putenv("VLC_PLUGIN_PATH=Z:"TOP_BUILDDIR"/modules");
    putenv("VLC_DATA_PATH=Z:"TOP_SRCDIR"/share");
#endif

#ifndef NDEBUG
    /* Disable stderr buffering. Indeed, stderr can be buffered on Windows (if
     * connected to a pipe). */
    setvbuf (stderr, NULL, _IONBF, BUFSIZ);
#endif

#if (_WIN32_WINNT < _WIN32_WINNT_WIN7)
    SetErrorMode(SEM_FAILCRITICALERRORS);
#endif
    HeapSetInformation(NULL, HeapEnableTerminationOnCorruption, NULL, 0);

    /* SetProcessDEPPolicy, SetDllDirectory, & Co. */
    HINSTANCE h_Kernel32 = GetModuleHandle(TEXT("kernel32.dll"));
    if (h_Kernel32 != NULL)
    {
        /* Enable DEP */
#ifndef PROCESS_DEP_ENABLE
# define PROCESS_DEP_ENABLE 1
#endif /* PROCESS_DEP_ENABLE */
        BOOL (WINAPI * mySetProcessDEPPolicy)( DWORD dwFlags);
        mySetProcessDEPPolicy = (BOOL (WINAPI *)(DWORD))
                            GetProcAddress(h_Kernel32, "SetProcessDEPPolicy");
        if(mySetProcessDEPPolicy)
            mySetProcessDEPPolicy(PROCESS_DEP_ENABLE);

        /* Do NOT load any library from cwd. */
        BOOL (WINAPI * mySetDllDirectoryA)(const char* lpPathName);
        mySetDllDirectoryA = (BOOL (WINAPI *)(const char*))
                            GetProcAddress(h_Kernel32, "SetDllDirectoryA");
        if(mySetDllDirectoryA)
            mySetDllDirectoryA("");
    }

    /***
     * The LoadLibrary* calls from the modules and the 3rd party code
     * will search in SYSTEM32 only
     * */
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    /***
     * Load DLLs from system32 before any other folder (when possible)
     */
    PrioritizeSystem32();

    /* Args */
    wchar_t **wargv = CommandLineToArgvW (GetCommandLine (), &argc);
    if (wargv == NULL)
        return 1;

    char *argv[argc + 5];
    BOOL crash_handling = TRUE;
    int j = 0;
    const wchar_t *lang = NULL;
    BOOL argument_error = FALSE;
    BOOL qa_options_added = FALSE;

    argv[j++] = FromWide( L"--media-library" );
    argv[j++] = FromWide( L"--no-ignore-config" );
    for (int i = 1; i < argc; i++)
    {
        if (!wcscmp(wargv[i], L"--"))
        {
            argv[j++] = FromWide(L"--no-one-instance");
            argv[j++] = FromWide(L"--no-one-instance-when-started-from-file");
            qa_options_added = TRUE;
            for (; i < argc; ++i) argv[j++] = FromWide(wargv[i]);
            break;
        }
        if(!wcscmp(wargv[i], L"--no-crashdump"))
        {
            crash_handling = FALSE;
            continue; /* don't give argument to libvlc */
        }
        const wchar_t *value = NULL;
        int consumed = haoke_parse_language_option(wargv[i],
            i + 1 < argc ? wargv[i + 1] : NULL, &value);
        if (consumed < 0)
        {
            argument_error = TRUE;
            break;
        }
        if (consumed)
        {
            lang = value;
            i += consumed - 1;
            continue;
        }

        argv[j++] = FromWide (wargv[i]);
    }

    const char *selected_language = HaokeLanguage(lang);
    argc = j;
    /* Isolate this portable QA instance from an official instance's IPC. */
    if (!qa_options_added)
    {
        argv[argc++] = FromWide(L"--no-one-instance");
        argv[argc++] = FromWide(L"--no-one-instance-when-started-from-file");
    }
    argv[argc] = NULL;
    LocalFree (wargv);
    if (argument_error)
    {
        HaokeStartError(selected_language, TRUE);
        for (int i = 0; i < argc; ++i) free(argv[i]);
        return 2;
    }
    for (int i = 0; i < argc; ++i)
    {
        if (argv[i] == NULL)
        {
            HaokeStartError(selected_language, FALSE);
            for (int k = 0; k < argc; ++k) free(argv[k]);
            return 1;
        }
    }

#ifdef HAVE_BREAKPAD
    void* eh = NULL;
    if(crash_handling)
    {
        wchar_t path[MAX_PATH];
        if( S_OK != SHGetFolderPathW( NULL, CSIDL_APPDATA | CSIDL_FLAG_CREATE,
                    NULL, SHGFP_TYPE_CURRENT, path ) )
            fprintf( stderr, "Can't open the vlc conf PATH\n" );
        else
        {
            size_t pathlen = wcslen(path);
            if ( pathlen + 1 + wcslen(L"\\vlc\\crashdump" ) <= MAX_PATH )
            {
                wcscpy( &path[pathlen], L"\\vlc\\crashdump" );
                CheckCrashDump( path );
                eh = InstallCrashHandler( path );
            }
        }
    }
#else
    (void)crash_handling;
#endif

    _setmode( _fileno( stdin ), _O_BINARY ); /* Needed for pipes */

    /* */
    HaokeSetLanguage(selected_language);

    /* Initialize libvlc */
    libvlc_instance_t *vlc;
    vlc = libvlc_new (argc, (const char **)argv);
    if (vlc != NULL)
    {
        libvlc_set_app_id (vlc, "org.VideoLAN.VLC", PACKAGE_VERSION,
                           PACKAGE_NAME);
        libvlc_set_user_agent (vlc, "VLC media player", "VLC/"PACKAGE_VERSION);
        libvlc_add_intf (vlc, "hotkeys,none");
        libvlc_add_intf (vlc, "globalhotkeys,none");
        libvlc_add_intf (vlc, NULL);
        libvlc_playlist_play (vlc, -1, 0, NULL);
        libvlc_wait (vlc);
        libvlc_release (vlc);
    }
    else
        HaokeStartError(selected_language, FALSE);


#ifdef HAVE_BREAKPAD
    ReleaseCrashHandler( eh );
#endif
    for (int i = 0; i < argc; i++)
        free (argv[i]);

    (void)hInstance; (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;
    return 0;
}
