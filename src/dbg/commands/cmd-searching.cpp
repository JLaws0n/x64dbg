#include "cmd-searching.h"
#include "memory.h"
#include "value.h"
#include "variable.h"
#include "reference.h"
#include "assemble.h"
#include "debugger.h"
#include "filehelper.h"
#include "label.h"
#include "yara/yara.h"

static int maxFindResults = 5000;

CMDRESULT cbInstrFind(int argc, char* argv[])
{
    if(IsArgumentsLessThan(argc, 3))
        return STATUS_ERROR;
    duint addr = 0;
    if(!valfromstring(argv[1], &addr, false))
        return STATUS_ERROR;
    char pattern[deflen] = "";
    //remove # from the start and end of the pattern (ODBGScript support)
    if(argv[2][0] == '#')
        strcpy_s(pattern, argv[2] + 1);
    else
        strcpy_s(pattern, argv[2]);
    int len = (int)strlen(pattern);
    if(pattern[len - 1] == '#')
        pattern[len - 1] = '\0';
    duint size = 0;
    duint base = MemFindBaseAddr(addr, &size, true);
    if(!base)
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "invalid memory address %p!\n"), addr);
        return STATUS_ERROR;
    }
    Memory<unsigned char*> data(size, "cbInstrFind:data");
    if(!MemRead(base, data(), size))
    {
        dputs(QT_TRANSLATE_NOOP("DBG", "failed to read memory!"));
        return STATUS_ERROR;
    }
    duint start = addr - base;
    duint find_size = 0;
    if(argc >= 4)
    {
        if(!valfromstring(argv[3], &find_size))
            find_size = size - start;
        if(find_size > (size - start))
            find_size = size - start;
    }
    else
        find_size = size - start;
    duint foundoffset = patternfind(data() + start, find_size, pattern);
    duint result = 0;
    if(foundoffset != -1)
        result = addr + foundoffset;
    varset("$result", result, false);
    return STATUS_CONTINUE;
}

CMDRESULT cbInstrFindAll(int argc, char* argv[])
{
    if(IsArgumentsLessThan(argc, 3))
        return STATUS_ERROR;
    duint addr = 0;
    if(!valfromstring(argv[1], &addr, false))
        return STATUS_ERROR;

    char pattern[deflen] = "";
    //remove # from the start and end of the pattern (ODBGScript support)
    if(argv[2][0] == '#')
        strcpy_s(pattern, argv[2] + 1);
    else
        strcpy_s(pattern, argv[2]);
    int len = (int)strlen(pattern);
    if(pattern[len - 1] == '#')
        pattern[len - 1] = '\0';
    duint size = 0;
    duint base = MemFindBaseAddr(addr, &size, true);
    if(!base)
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "invalid memory address %p!\n"), addr);
        return STATUS_ERROR;
    }
    if(argc >= 4)
    {
        duint usersize;
        if(valfromstring(argv[3], &usersize))
            size = usersize;
    }
    Memory<unsigned char*> data(size, "cbInstrFindAll:data");
    if(!MemRead(base, data(), size))
    {
        dputs(QT_TRANSLATE_NOOP("DBG", "failed to read memory!"));
        return STATUS_ERROR;
    }
    duint start = addr - base;
    duint find_size = 0;
    bool findData = false;
    if(argc >= 4)
    {
        if(!_stricmp(argv[3], "&data&"))
        {
            find_size = size - start;
            findData = true;
        }
        else
            find_size = size - start;
    }
    else
        find_size = size - start;
    //setup reference view
    char patternshort[256] = "";
    strncpy_s(patternshort, pattern, min(16, len));
    if(len > 16)
        strcat_s(patternshort, "...");
    char patterntitle[256] = "";
    sprintf_s(patterntitle, GuiTranslateText(QT_TRANSLATE_NOOP("DBG", "Pattern: %s")), patternshort);
    GuiReferenceInitialize(patterntitle);
    GuiReferenceAddColumn(2 * sizeof(duint), GuiTranslateText(QT_TRANSLATE_NOOP("DBG", "Address")));
    if(findData)
        GuiReferenceAddColumn(0, GuiTranslateText(QT_TRANSLATE_NOOP("DBG", "Data")));
    else
        GuiReferenceAddColumn(0, GuiTranslateText(QT_TRANSLATE_NOOP("DBG", "Disassembly")));
    GuiReferenceSetRowCount(0);
    GuiReferenceReloadData();
    DWORD ticks = GetTickCount();
    int refCount = 0;
    duint i = 0;
    duint result = 0;
    std::vector<PatternByte> searchpattern;
    if(!patterntransform(pattern, searchpattern))
    {
        dputs(QT_TRANSLATE_NOOP("DBG", "failed to transform pattern!"));
        return STATUS_ERROR;
    }
    while(refCount < maxFindResults)
    {
        duint foundoffset = patternfind(data() + start + i, find_size - i, searchpattern);
        if(foundoffset == -1)
            break;
        i += foundoffset + 1;
        result = addr + i - 1;
        char msg[deflen] = "";
        sprintf_s(msg, "%p", result);
        GuiReferenceSetRowCount(refCount + 1);
        GuiReferenceSetCellContent(refCount, 0, msg);
        if(findData)
        {
            Memory<unsigned char*> printData(searchpattern.size(), "cbInstrFindAll:printData");
            MemRead(result, printData(), printData.size());
            for(size_t j = 0, k = 0; j < printData.size(); j++)
            {
                if(j)
                    k += sprintf(msg + k, " ");
                k += sprintf(msg + k, "%.2X", printData()[j]);
            }
        }
        else
        {
            if(!GuiGetDisassembly(result, msg))
                strcpy_s(msg, GuiTranslateText(QT_TRANSLATE_NOOP("DBG", "[Error disassembling]")));
        }
        GuiReferenceSetCellContent(refCount, 1, msg);
        result++;
        refCount++;
    }
    GuiReferenceReloadData();
    dprintf(QT_TRANSLATE_NOOP("DBG", "%d occurrences found in %ums\n"), refCount, GetTickCount() - ticks);
    varset("$result", refCount, false);
    return STATUS_CONTINUE;
}

CMDRESULT cbInstrFindAllMem(int argc, char* argv[])
{
    if(IsArgumentsLessThan(argc, 3))
        return STATUS_ERROR;
    duint addr = 0;
    if(!valfromstring(argv[1], &addr, false))
        return STATUS_ERROR;

    char pattern[deflen] = "";
    //remove # from the start and end of the pattern (ODBGScript support)
    if(argv[2][0] == '#')
        strcpy_s(pattern, argv[2] + 1);
    else
        strcpy_s(pattern, argv[2]);
    int len = (int)strlen(pattern);
    if(pattern[len - 1] == '#')
        pattern[len - 1] = '\0';
    std::vector<PatternByte> searchpattern;
    if(!patterntransform(pattern, searchpattern))
    {
        dputs(QT_TRANSLATE_NOOP("DBG", "failed to transform pattern!"));
        return STATUS_ERROR;
    }

    duint endAddr = -1;
    bool findData = false;
    if(argc >= 4)
    {
        if(!_stricmp(argv[3], "&data&"))
            findData = true;
        else if(!valfromstring(argv[3], &endAddr))
            findData = false;
    }

    SHARED_ACQUIRE(LockMemoryPages);
    std::vector<SimplePage> searchPages;
    for(auto & itr : memoryPages)
    {
        if(itr.second.mbi.State != MEM_COMMIT)
            continue;
        SimplePage page(duint(itr.second.mbi.BaseAddress), itr.second.mbi.RegionSize);
        if(page.address >= addr && page.address + page.size <= endAddr)
            searchPages.push_back(page);
    }
    SHARED_RELEASE();

    DWORD ticks = GetTickCount();

    std::vector<duint> results;
    if(!MemFindInMap(searchPages, searchpattern, results, maxFindResults))
    {
        dputs(QT_TRANSLATE_NOOP("DBG", "MemFindInMap failed!"));
        return STATUS_ERROR;
    }

    //setup reference view
    char patternshort[256] = "";
    strncpy_s(patternshort, pattern, min(16, len));
    if(len > 16)
        strcat_s(patternshort, "...");
    char patterntitle[256] = "";
    sprintf_s(patterntitle, GuiTranslateText(QT_TRANSLATE_NOOP("DBG", "Pattern: %s")), patternshort);
    GuiReferenceInitialize(patterntitle);
    GuiReferenceAddColumn(2 * sizeof(duint), GuiTranslateText(QT_TRANSLATE_NOOP("DBG", "Address")));
    if(findData)
        GuiReferenceAddColumn(0, GuiTranslateText(QT_TRANSLATE_NOOP("DBG", "Data")));
    else
        GuiReferenceAddColumn(0, GuiTranslateText(QT_TRANSLATE_NOOP("DBG", "Disassembly")));
    GuiReferenceSetRowCount(0);
    GuiReferenceReloadData();

    int refCount = 0;
    for(duint result : results)
    {
        char msg[deflen] = "";
        sprintf_s(msg, "%p", result);
        GuiReferenceSetRowCount(refCount + 1);
        GuiReferenceSetCellContent(refCount, 0, msg);
        if(findData)
        {
            Memory<unsigned char*> printData(searchpattern.size(), "cbInstrFindAll:printData");
            MemRead(result, printData(), printData.size());
            for(size_t j = 0, k = 0; j < printData.size(); j++)
            {
                if(j)
                    k += sprintf(msg + k, " ");
                k += sprintf(msg + k, "%.2X", printData()[j]);
            }
        }
        else
        {
            if(!GuiGetDisassembly(result, msg))
                strcpy_s(msg, GuiTranslateText(QT_TRANSLATE_NOOP("DBG", "[Error disassembling]")));
        }
        GuiReferenceSetCellContent(refCount, 1, msg);
        refCount++;
    }

    GuiReferenceReloadData();
    dprintf(QT_TRANSLATE_NOOP("DBG", "%d occurrences found in %ums\n"), refCount, GetTickCount() - ticks);
    varset("$result", refCount, false);

    return STATUS_CONTINUE;
}

static bool cbFindAsm(Capstone* disasm, BASIC_INSTRUCTION_INFO* basicinfo, REFINFO* refinfo)
{
    if(!disasm || !basicinfo)   //initialize
    {
        GuiReferenceInitialize(refinfo->name);
        GuiReferenceAddColumn(2 * sizeof(duint), GuiTranslateText(QT_TRANSLATE_NOOP("DBG", "Address")));
        GuiReferenceAddColumn(0, GuiTranslateText(QT_TRANSLATE_NOOP("DBG", "Disassembly")));
        GuiReferenceSetRowCount(0);
        GuiReferenceReloadData();
        return true;
    }
    const char* instruction = (const char*)refinfo->userinfo;
    bool found = !_stricmp(instruction, basicinfo->instruction);
    if(found)
    {
        char addrText[20] = "";
        sprintf_s(addrText, "%p", disasm->Address());
        GuiReferenceSetRowCount(refinfo->refcount + 1);
        GuiReferenceSetCellContent(refinfo->refcount, 0, addrText);
        char disassembly[GUI_MAX_DISASSEMBLY_SIZE] = "";
        if(GuiGetDisassembly((duint)disasm->Address(), disassembly))
            GuiReferenceSetCellContent(refinfo->refcount, 1, disassembly);
        else
            GuiReferenceSetCellContent(refinfo->refcount, 1, disasm->InstructionText().c_str());
    }
    return found;
}

CMDRESULT cbInstrFindAsm(int argc, char* argv[])
{
    if(IsArgumentsLessThan(argc, 2))
        return STATUS_ERROR;

    duint addr = 0;
    if(argc < 3 || !valfromstring(argv[2], &addr))
        addr = GetContextDataEx(hActiveThread, UE_CIP);
    duint size = 0;
    if(argc >= 4)
        if(!valfromstring(argv[3], &size))
            size = 0;

    duint refFindType = CURRENT_REGION;
    if(argc >= 5 && valfromstring(argv[4], &refFindType, true))
        if(refFindType != CURRENT_REGION && refFindType != CURRENT_MODULE && refFindType != ALL_MODULES)
            refFindType = CURRENT_REGION;

    unsigned char dest[16];
    int asmsize = 0;
    char error[MAX_ERROR_SIZE] = "";
    if(!assemble(addr + size / 2, dest, &asmsize, argv[1], error))
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "failed to assemble \"%s\" (%s)!\n"), argv[1], error);
        return STATUS_ERROR;
    }
    BASIC_INSTRUCTION_INFO basicinfo;
    memset(&basicinfo, 0, sizeof(BASIC_INSTRUCTION_INFO));
    disasmfast(dest, addr + size / 2, &basicinfo);

    duint ticks = GetTickCount();
    char title[256] = "";
    sprintf_s(title, GuiTranslateText(QT_TRANSLATE_NOOP("DBG", "Command: \"%s\"")), basicinfo.instruction);
    int found = RefFind(addr, size, cbFindAsm, (void*)&basicinfo.instruction[0], false, title, (REFFINDTYPE)refFindType, true);
    dprintf(QT_TRANSLATE_NOOP("DBG", "%u result(s) in %ums\n"), DWORD(found), GetTickCount() - DWORD(ticks));
    varset("$result", found, false);
    return STATUS_CONTINUE;
}

CMDRESULT cbInstrRefFind(int argc, char* argv[])
{
    if(IsArgumentsLessThan(argc, 2))
        return STATUS_ERROR;
    std::string newCommand = std::string("reffindrange ") + argv[1] + std::string(",") + argv[1];
    if(argc > 2)
        newCommand += std::string(",") + argv[2];
    if(argc > 3)
        newCommand += std::string(",") + argv[3];
    if(argc > 4)
        newCommand += std::string(",") + argv[4];
    return cmddirectexec(newCommand.c_str());
}

struct VALUERANGE
{
    duint start;
    duint end;
};

static bool cbRefFind(Capstone* disasm, BASIC_INSTRUCTION_INFO* basicinfo, REFINFO* refinfo)
{
    if(!disasm || !basicinfo)   //initialize
    {
        GuiReferenceInitialize(refinfo->name);
        GuiReferenceAddColumn(sizeof(duint) * 2, GuiTranslateText(QT_TRANSLATE_NOOP("DBG", "Address")));
        GuiReferenceAddColumn(10, GuiTranslateText(QT_TRANSLATE_NOOP("DBG", "Disassembly")));
        GuiReferenceSetRowCount(0);
        GuiReferenceReloadData();
        return true;
    }
    bool found = false;
    VALUERANGE* range = (VALUERANGE*)refinfo->userinfo;
    duint start = range->start;
    duint end = range->end;
    if((basicinfo->type & TYPE_VALUE) == TYPE_VALUE)
    {
        duint value = basicinfo->value.value;
        if(value >= start && value <= end)
            found = true;
    }
    if((basicinfo->type & TYPE_MEMORY) == TYPE_MEMORY)
    {
        duint value = basicinfo->memory.value;
        if(value >= start && value <= end)
            found = true;
    }
    if((basicinfo->type & TYPE_ADDR) == TYPE_ADDR)
    {
        duint value = basicinfo->addr;
        if(value >= start && value <= end)
            found = true;
    }
    if(found)
    {
        char addrText[20] = "";
        sprintf_s(addrText, "%p", disasm->Address());
        GuiReferenceSetRowCount(refinfo->refcount + 1);
        GuiReferenceSetCellContent(refinfo->refcount, 0, addrText);
        char disassembly[GUI_MAX_DISASSEMBLY_SIZE] = "";
        if(GuiGetDisassembly((duint)disasm->Address(), disassembly))
            GuiReferenceSetCellContent(refinfo->refcount, 1, disassembly);
        else
            GuiReferenceSetCellContent(refinfo->refcount, 1, disasm->InstructionText().c_str());
    }
    return found;
}

CMDRESULT cbInstrRefFindRange(int argc, char* argv[])
{
    if(IsArgumentsLessThan(argc, 2))
        return STATUS_ERROR;
    VALUERANGE range;
    if(!valfromstring(argv[1], &range.start, false))
        return STATUS_ERROR;
    if(argc < 3 || !valfromstring(argv[2], &range.end, false))
        range.end = range.start;
    duint addr = 0;
    if(argc < 4 || !valfromstring(argv[3], &addr))
        addr = GetContextDataEx(hActiveThread, UE_CIP);
    duint size = 0;
    if(argc >= 5)
        if(!valfromstring(argv[4], &size))
            size = 0;
    duint ticks = GetTickCount();
    char title[256] = "";
    if(range.start == range.end)
        sprintf_s(title, GuiTranslateText(QT_TRANSLATE_NOOP("DBG", "Constant: %p")), range.start);
    else
        sprintf_s(title, GuiTranslateText(QT_TRANSLATE_NOOP("DBG", "Range: %p-%p")), range.start, range.end);

    duint refFindType = CURRENT_REGION;
    if(argc >= 6 && valfromstring(argv[5], &refFindType, true))
        if(refFindType != CURRENT_REGION && refFindType != CURRENT_MODULE && refFindType != ALL_MODULES)
            refFindType = CURRENT_REGION;

    int found = RefFind(addr, size, cbRefFind, &range, false, title, (REFFINDTYPE)refFindType, false);
    dprintf(QT_TRANSLATE_NOOP("DBG", "%u reference(s) in %ums\n"), DWORD(found), GetTickCount() - DWORD(ticks));
    varset("$result", found, false);
    return STATUS_CONTINUE;
}

static bool cbRefStr(Capstone* disasm, BASIC_INSTRUCTION_INFO* basicinfo, REFINFO* refinfo)
{
    if(!disasm || !basicinfo)   //initialize
    {
        GuiReferenceInitialize(refinfo->name);
        GuiReferenceAddColumn(2 * sizeof(duint), GuiTranslateText(QT_TRANSLATE_NOOP("DBG", "Address")));
        GuiReferenceAddColumn(64, GuiTranslateText(QT_TRANSLATE_NOOP("DBG", "Disassembly")));
        GuiReferenceAddColumn(500, GuiTranslateText(QT_TRANSLATE_NOOP("DBG", "String")));
        GuiReferenceSetSearchStartCol(2); //only search the strings
        GuiReferenceSetRowCount(0);
        GuiReferenceReloadData();
        return true;
    }
    bool found = false;
    char string[MAX_STRING_SIZE] = "";
    if(basicinfo->branch)   //branches have no strings (jmp dword [401000])
        return false;
    if((basicinfo->type & TYPE_VALUE) == TYPE_VALUE)
    {
        if(DbgGetStringAt(basicinfo->value.value, string))
            found = true;
    }
    if((basicinfo->type & TYPE_MEMORY) == TYPE_MEMORY)
    {
        if(DbgGetStringAt(basicinfo->memory.value, string))
            found = true;
    }
    if(found)
    {
        char addrText[20] = "";
        sprintf_s(addrText, "%p", disasm->Address());
        GuiReferenceSetRowCount(refinfo->refcount + 1);
        GuiReferenceSetCellContent(refinfo->refcount, 0, addrText);
        char disassembly[4096] = "";
        if(GuiGetDisassembly((duint)disasm->Address(), disassembly))
            GuiReferenceSetCellContent(refinfo->refcount, 1, disassembly);
        else
            GuiReferenceSetCellContent(refinfo->refcount, 1, disasm->InstructionText().c_str());
        GuiReferenceSetCellContent(refinfo->refcount, 2, string);
    }
    return found;
}

CMDRESULT cbInstrRefStr(int argc, char* argv[])
{
    duint ticks = GetTickCount();
    duint addr;
    duint size = 0;
    String TranslatedString;

    // If not specified, assume CURRENT_REGION by default
    if(argc < 2 || !valfromstring(argv[1], &addr, true))
        addr = GetContextDataEx(hActiveThread, UE_CIP);
    if(argc >= 3)
        if(!valfromstring(argv[2], &size, true))
            size = 0;

    duint refFindType = CURRENT_REGION;
    if(argc >= 4 && valfromstring(argv[3], &refFindType, true))
        if(refFindType != CURRENT_REGION && refFindType != CURRENT_MODULE && refFindType != ALL_MODULES)
            refFindType = CURRENT_REGION;

    TranslatedString = GuiTranslateText(QT_TRANSLATE_NOOP("DBG", "Strings"));
    int found = RefFind(addr, size, cbRefStr, 0, false, TranslatedString.c_str(), (REFFINDTYPE)refFindType, false);
    dprintf(QT_TRANSLATE_NOOP("DBG", "%u string(s) in %ums\n"), DWORD(found), GetTickCount() - DWORD(ticks));
    varset("$result", found, false);
    return STATUS_CONTINUE;
}

static bool cbModCallFind(Capstone* disasm, BASIC_INSTRUCTION_INFO* basicinfo, REFINFO* refinfo)
{
    if(!disasm || !basicinfo)   //initialize
    {
        GuiReferenceInitialize(refinfo->name);
        GuiReferenceAddColumn(2 * sizeof(duint), GuiTranslateText(QT_TRANSLATE_NOOP("DBG", "Address")));
        GuiReferenceAddColumn(20, GuiTranslateText(QT_TRANSLATE_NOOP("DBG", "Disassembly")));
        GuiReferenceAddColumn(MAX_LABEL_SIZE, GuiTranslateText(QT_TRANSLATE_NOOP("DBG", "Destination")));
        GuiReferenceSetRowCount(0);
        GuiReferenceReloadData();
        return true;
    }
    bool found = false;
    char label[MAX_LABEL_SIZE] = "";
    char module[MAX_MODULE_SIZE] = "";
    if(basicinfo->call)   //we are looking for calls
    {
        duint ptr = basicinfo->addr > 0 ? basicinfo->addr : basicinfo->memory.value;
        found = DbgGetLabelAt(ptr, SEG_DEFAULT, label) && !LabelGet(ptr, label) && !strstr(label, "sub_") && DbgGetModuleAt(ptr, module); //a non-user label
    }
    if(found)
    {
        char addrText[20] = "";
        char moduleTargetText[256] = "";
        sprintf_s(addrText, "%p", disasm->Address());
        sprintf(moduleTargetText, "%s.%s", module, label);
        GuiReferenceSetRowCount(refinfo->refcount + 1);
        GuiReferenceSetCellContent(refinfo->refcount, 0, addrText);
        char disassembly[GUI_MAX_DISASSEMBLY_SIZE] = "";
        if(GuiGetDisassembly((duint)disasm->Address(), disassembly))
        {
            GuiReferenceSetCellContent(refinfo->refcount, 1, disassembly);
            GuiReferenceSetCellContent(refinfo->refcount, 2, moduleTargetText);
        }
        else
        {
            GuiReferenceSetCellContent(refinfo->refcount, 1, disasm->InstructionText().c_str());
            GuiReferenceSetCellContent(refinfo->refcount, 2, moduleTargetText);
        }
    }
    return found;
}

CMDRESULT cbInstrModCallFind(int argc, char* argv[])
{
    duint addr;
    if(argc < 2 || !valfromstring(argv[1], &addr, true))
        addr = GetContextDataEx(hActiveThread, UE_CIP);
    duint size = 0;
    if(argc >= 3)
        if(!valfromstring(argv[2], &size, true))
            size = 0;

    duint refFindType = CURRENT_REGION;
    if(argc >= 4 && valfromstring(argv[3], &refFindType, true))
        if(refFindType != CURRENT_REGION && refFindType != CURRENT_MODULE && refFindType != ALL_MODULES)
            refFindType = CURRENT_REGION;

    duint ticks = GetTickCount();
    String Calls = GuiTranslateText(QT_TRANSLATE_NOOP("DBG", "Calls"));
    int found = RefFind(addr, size, cbModCallFind, 0, false, Calls.c_str(), (REFFINDTYPE)refFindType, false);
    dprintf(QT_TRANSLATE_NOOP("DBG", "%u call(s) in %ums\n"), DWORD(found), GetTickCount() - DWORD(ticks));
    varset("$result", found, false);
    return STATUS_CONTINUE;
}

static void yaraCompilerCallback(int error_level, const char* file_name, int line_number, const char* message, void* user_data)
{
    switch(error_level)
    {
    case YARA_ERROR_LEVEL_ERROR:
        dprintf(QT_TRANSLATE_NOOP("DBG", "[YARA ERROR] "));
        break;
    case YARA_ERROR_LEVEL_WARNING:
        dprintf(QT_TRANSLATE_NOOP("DBG", "[YARA WARNING] "));
        break;
    }
    dprintf(QT_TRANSLATE_NOOP("DBG", "File: \"%s\", Line: %d, Message: \"%s\"\n"), file_name, line_number, message);
}

static String yara_print_string(const uint8_t* data, int length)
{
    String result = "\"";
    const char* str = (const char*)data;
    for(int i = 0; i < length; i++)
    {
        char cur[16] = "";
        if(str[i] >= 32 && str[i] <= 126)
            sprintf_s(cur, "%c", str[i]);
        else
            sprintf_s(cur, "\\x%02X", (uint8_t)str[i]);
        result += cur;
    }
    result += "\"";
    return result;
}

static String yara_print_hex_string(const uint8_t* data, int length)
{
    String result = "";
    for(int i = 0; i < length; i++)
    {
        if(i)
            result += " ";
        char cur[16] = "";
        sprintf_s(cur, "%02X", (uint8_t)data[i]);
        result += cur;
    }
    return result;
}

struct YaraScanInfo
{
    duint base;
    int index;
    bool rawFile;
    const char* modname;
    bool debug;

    YaraScanInfo(duint base, bool rawFile, const char* modname, bool debug)
        : base(base), index(0), rawFile(rawFile), modname(modname), debug(debug)
    {
    }
};

static int yaraScanCallback(int message, void* message_data, void* user_data)
{
    YaraScanInfo* scanInfo = (YaraScanInfo*)user_data;
    bool debug = scanInfo->debug;
    switch(message)
    {
    case CALLBACK_MSG_RULE_MATCHING:
    {
        duint base = scanInfo->base;
        YR_RULE* yrRule = (YR_RULE*)message_data;
        auto addReference = [scanInfo, yrRule](duint addr, const char* identifier, const std::string & pattern)
        {
            auto index = scanInfo->index;
            GuiReferenceSetRowCount(index + 1);
            scanInfo->index++;

            char addr_text[deflen] = "";
            sprintf_s(addr_text, "%p", addr);
            GuiReferenceSetCellContent(index, 0, addr_text); //Address
            String ruleFullName = "";
            ruleFullName += yrRule->identifier;
            if(identifier)
            {
                ruleFullName += ".";
                ruleFullName += identifier;
            }
            GuiReferenceSetCellContent(index, 1, ruleFullName.c_str()); //Rule
            GuiReferenceSetCellContent(index, 2, pattern.c_str()); //Data
        };

        if(STRING_IS_NULL(yrRule->strings))
        {
            if(debug)
                dprintf(QT_TRANSLATE_NOOP("DBG", "[YARA] Global rule \"%s\" matched!\n"), yrRule->identifier);
            addReference(base, nullptr, "");
        }
        else
        {
            if(debug)
                dprintf(QT_TRANSLATE_NOOP("DBG", "[YARA] Rule \"%s\" matched:\n"), yrRule->identifier);
            YR_STRING* string;
            yr_rule_strings_foreach(yrRule, string)
            {
                YR_MATCH* match;
                yr_string_matches_foreach(string, match)
                {
                    String pattern;
                    if(STRING_IS_HEX(string))
                        pattern = yara_print_hex_string(match->data, match->match_length);
                    else
                        pattern = yara_print_string(match->data, match->match_length);
                    auto offset = duint(match->base + match->offset);
                    duint addr;
                    if(scanInfo->rawFile)   //convert raw offset to virtual offset
                        addr = valfileoffsettova(scanInfo->modname, offset);
                    else
                        addr = base + offset;

                    if(debug)
                        dprintf(QT_TRANSLATE_NOOP("DBG", "[YARA] String \"%s\" : %s on %p\n"), string->identifier, pattern.c_str(), addr);

                    addReference(addr, string->identifier, pattern);
                }
            }
        }
    }
    break;

    case CALLBACK_MSG_RULE_NOT_MATCHING:
    {
        YR_RULE* yrRule = (YR_RULE*)message_data;
        if(debug)
            dprintf(QT_TRANSLATE_NOOP("DBG", "[YARA] Rule \"%s\" did not match!\n"), yrRule->identifier);
    }
    break;

    case CALLBACK_MSG_SCAN_FINISHED:
    {
        if(debug)
            dputs(QT_TRANSLATE_NOOP("DBG", "[YARA] Scan finished!"));
    }
    break;

    case CALLBACK_MSG_IMPORT_MODULE:
    {
        YR_MODULE_IMPORT* yrModuleImport = (YR_MODULE_IMPORT*)message_data;
        if(debug)
            dprintf(QT_TRANSLATE_NOOP("DBG", "[YARA] Imported module \"%s\"!\n"), yrModuleImport->module_name);
    }
    break;
    }
    return ERROR_SUCCESS; //nicely undocumented what this should be
}

CMDRESULT cbInstrYara(int argc, char* argv[])
{
    if(IsArgumentsLessThan(argc, 2))
        return STATUS_ERROR;
    duint addr = 0;
    SELECTIONDATA sel;
    GuiSelectionGet(GUI_DISASSEMBLY, &sel);
    addr = sel.start;

    duint base = 0;
    duint size = 0;
    duint mod = ModBaseFromName(argv[2]);
    bool rawFile = false;
    if(mod)
    {
        base = mod;
        size = ModSizeFromAddr(base);
        rawFile = argc > 3 && *argv[3] == '1';
    }
    else
    {
        if(!valfromstring(argv[2], &addr))
        {
            dprintf(QT_TRANSLATE_NOOP("DBG", "invalid value \"%s\"!\n"), argv[2]);
            return STATUS_ERROR;
        }

        size = 0;
        if(argc >= 4)
            if(!valfromstring(argv[3], &size))
                size = 0;
        if(!size)
            addr = MemFindBaseAddr(addr, &size);
        base = addr;
    }
    std::vector<unsigned char> rawFileData;
    if(rawFile)   //read the file from disk
    {
        char modPath[MAX_PATH] = "";
        if(!ModPathFromAddr(base, modPath, MAX_PATH))
        {
            dprintf(QT_TRANSLATE_NOOP("DBG", "failed to get module path for %p!\n"), base);
            return STATUS_ERROR;
        }
        if(!FileHelper::ReadAllData(modPath, rawFileData))
        {
            dprintf(QT_TRANSLATE_NOOP("DBG", "failed to read file \"%s\"!\n"), modPath);
            return STATUS_ERROR;
        }
        size = rawFileData.size();
    }
    Memory<uint8_t*> data(size);
    if(rawFile)
        memcpy(data(), rawFileData.data(), size);
    else if(!MemRead(base, data(), size))
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "failed to read memory page %p[%X]!\n"), base, DWORD(size));
        return STATUS_ERROR;
    }

    String rulesContent;
    if(!FileHelper::ReadAllText(argv[1], rulesContent))
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "Failed to read the rules file \"%s\"\n"), argv[1]);
        return STATUS_ERROR;
    }

    bool bSuccess = false;
    YR_COMPILER* yrCompiler;
    if(yr_compiler_create(&yrCompiler) == ERROR_SUCCESS)
    {
        yr_compiler_set_callback(yrCompiler, yaraCompilerCallback, 0);
        if(yr_compiler_add_string(yrCompiler, rulesContent.c_str(), nullptr) == 0)    //no errors found
        {
            YR_RULES* yrRules;
            if(yr_compiler_get_rules(yrCompiler, &yrRules) == ERROR_SUCCESS)
            {
                //initialize new reference tab
                char modname[MAX_MODULE_SIZE] = "";
                if(!ModNameFromAddr(base, modname, true))
                    sprintf_s(modname, "%p", base);
                String fullName;
                const char* fileName = strrchr(argv[1], '\\');
                if(fileName)
                    fullName = fileName + 1;
                else
                    fullName = argv[1];
                fullName += " (";
                fullName += modname;
                fullName += ")"; //nanana, very ugly code (long live open source)
                GuiReferenceInitialize(fullName.c_str());
                GuiReferenceAddColumn(sizeof(duint) * 2, GuiTranslateText(QT_TRANSLATE_NOOP("DBG", "Address")));
                GuiReferenceAddColumn(48, GuiTranslateText(QT_TRANSLATE_NOOP("DBG", "Rule")));
                GuiReferenceAddColumn(10, GuiTranslateText(QT_TRANSLATE_NOOP("DBG", "Data")));
                GuiReferenceSetRowCount(0);
                GuiReferenceReloadData();
                YaraScanInfo scanInfo(base, rawFile, argv[2], settingboolget("Engine", "YaraDebug"));
                duint ticks = GetTickCount();
                dputs(QT_TRANSLATE_NOOP("DBG", "[YARA] Scan started..."));
                int err = yr_rules_scan_mem(yrRules, data(), size, 0, yaraScanCallback, &scanInfo, 0);
                GuiReferenceReloadData();
                switch(err)
                {
                case ERROR_SUCCESS:
                    dprintf(QT_TRANSLATE_NOOP("DBG", "%u scan results in %ums...\n"), DWORD(scanInfo.index), GetTickCount() - DWORD(ticks));
                    bSuccess = true;
                    break;
                case ERROR_TOO_MANY_MATCHES:
                    dputs(QT_TRANSLATE_NOOP("DBG", "too many matches!"));
                    break;
                default:
                    dputs(QT_TRANSLATE_NOOP("DBG", "error while scanning memory!"));
                    break;
                }
                yr_rules_destroy(yrRules);
            }
            else
                dputs(QT_TRANSLATE_NOOP("DBG", "error while getting the rules!"));
        }
        else
            dputs(QT_TRANSLATE_NOOP("DBG", "errors in the rules file!"));
        yr_compiler_destroy(yrCompiler);
    }
    else
        dputs(QT_TRANSLATE_NOOP("DBG", "yr_compiler_create failed!"));
    return bSuccess ? STATUS_CONTINUE : STATUS_ERROR;
}

CMDRESULT cbInstrYaramod(int argc, char* argv[])
{
    if(IsArgumentsLessThan(argc, 3))
        return STATUS_ERROR;
    if(!ModBaseFromName(argv[2]))
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "invalid module \"%s\"!\n"), argv[2]);
        return STATUS_ERROR;
    }
    return cmddirectexec(StringUtils::sprintf("yara \"%s\",\"%s\",%s", argv[1], argv[2], argc > 3 && *argv[3] == '1' ? "1" : "0").c_str());
}

CMDRESULT cbInstrSetMaxFindResult(int argc, char* argv[])
{
    if(IsArgumentsLessThan(argc, 2))
        return STATUS_ERROR;
    duint num;
    if(!valfromstring(argv[1], &num))
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "Invalid expression: \"%s\""), argv[1]);
        return STATUS_ERROR;
    }
    maxFindResults = int(num & 0x7FFFFFFF);
    return STATUS_CONTINUE;
}
