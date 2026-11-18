#include "stdafx.h"
#include "dragon.h"
#include "Menu.h"
#include "MenuDef.h"
#include "MenuDefine.h"
#include "ItemTable.h"
#include "skill_lsw.h"
#include "network.h"
#include "LocalAdminPanel.h"
#include "SymbolMgr_Client.h"
#include "../Shared/LocalAdminProtocol.h"
#include <commctrl.h>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <string>
#include <vector>
#pragma comment(lib, "comctl32.lib")

extern HWND g_hwndMain;
extern HINSTANCE g_hInstance;
extern LPCHARACTER Hero;
extern CSymbolMgr_Client g_SymBolMgr;
extern int QueuePacket(t_packet* packet, int flag);
static_assert(sizeof(LocalAdminCatalog) <= sizeof(((t_packet*)0)->u), "Catalog packet exceeds game payload");
static_assert(sizeof(LocalAdminRequest) <= sizeof(((t_packet*)0)->u), "Request packet exceeds game payload");

namespace
{
    enum ControlId
    {
        ID_TABS = 4000, ID_STATUS, ID_REFRESH, ID_APPLY_STATS, ID_UNDO_STATS,
        ID_LEVEL, ID_EXP, ID_POINTS, ID_ATTRIBUTE = 4020,
        ID_SEARCH = 4040, ID_ITEMS, ID_QUANTITY, ID_DURABILITY, ID_GRADE,
        ID_EFFECT1, ID_EFFECT2, ID_EFFECT3, ID_DYNAMIC, ID_HIGH,
        ID_PREVIEW, ID_CREATE, ID_PREVIEW_TEXT,
        ID_GOLD = 4070, ID_BANK, ID_RE, ID_APPLY_GOLD, ID_APPLY_BANK, ID_APPLY_RE,
        ID_DUAL_CLASS = 4100, ID_DUAL_STEP, ID_FAME, ID_LADDER, ID_CSP, ID_SYMBOL_GRADE,
        ID_APPLY_DUAL, ID_APPLY_SYMBOL, ID_DUAL_INFO,
        ID_PROGRESS_KIND = 4120, ID_PROGRESS_INDEX, ID_PROGRESS_LEVEL, ID_PROGRESS_EXP,
        ID_PROGRESS_RARE, ID_APPLY_PROGRESS, ID_PROGRESS_HINT,
        ID_MONSTER_SEARCH = 4140, ID_MONSTER, ID_MONSTER_COUNT, ID_SUMMON
    };

    HWND panel = NULL;
    HWND tabControl = NULL;
    std::vector<HWND> pages[6];
    LocalAdminState currentState = {};
    DWORD nextRequest = 0;
    DWORD pendingRequest = 0;
    DWORD previewToken = 0;
    bool canUndo = false;
    bool fillingControls = false;
    bool catalogReady = false;
    DWORD catalogRequest = 0;
    std::vector<LocalAdminEffect> effects;
    const char* attributeNames[] = { "STR", "DEX", "CON", "WIS", "INT", "CHA", "MOVP", "ENDU", "MOR", "LUCK", "WSPS" };

    HWND Control(int id) { return GetDlgItem(panel, id); }

    HWND AddControl(const char* type, const char* text, DWORD style, int id,
        int x, int y, int width, int height, int page = -1)
    {
        HWND window = CreateWindowExA(strcmp(type, "EDIT") == 0 ? WS_EX_CLIENTEDGE : 0,
            type, text, WS_CHILD | WS_VISIBLE | style, x, y, width, height,
            panel, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_hInstance, NULL);
        SendMessage(window, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        if (page >= 0) pages[page].push_back(window);
        return window;
    }

    void SetNumber(int id, unsigned long value)
    {
        char text[32];
        sprintf_s(text, "%lu", value);
        SetWindowTextA(Control(id), text);
    }

    bool ReadNumber(int id, DWORD maximum, DWORD& value)
    {
        char text[32] = {};
        GetWindowTextA(Control(id), text, sizeof(text));
        if (!text[0] || text[0] == '-') return false;
        char* end = NULL;
        errno = 0;
        const unsigned long parsed = strtoul(text, &end, 10);
        if (errno == ERANGE || !end || *end || parsed > maximum) return false;
        value = parsed;
        return true;
    }

    int SelectedValue(int id)
    {
        const LRESULT selected = SendMessage(Control(id), CB_GETCURSEL, 0, 0);
        return selected == CB_ERR ? 0 : static_cast<int>(SendMessage(Control(id), CB_GETITEMDATA, selected, 0));
    }

    void FillProgressValue()
    {
        const int kind = SelectedValue(ID_PROGRESS_KIND), index = SelectedValue(ID_PROGRESS_INDEX);
        DWORD level = 0, exp = 0, rare = 0;
        if (kind == 0 && index < 13) level = currentState.skills[88 + index];
        if (kind == 1 && index < 200) level = currentState.skills[index];
        if (kind == 2 && index < 45) { level = currentState.workExp[index] / 10000; exp = currentState.workExp[index] % 10000; rare = currentState.workRare[index]; }
        if (kind == 3 && index < 200) level = currentState.wizard[index];
        if (kind == 4 && index < 200) level = currentState.priest[index];
        if (kind == 5 && index < 36) level = currentState.skills[115 + index];
        if (kind == 6 && index < 7) level = currentState.extraProgress[index];
        SetNumber(ID_PROGRESS_LEVEL, level); SetNumber(ID_PROGRESS_EXP, exp); SetNumber(ID_PROGRESS_RARE, rare);
        EnableWindow(Control(ID_PROGRESS_EXP), kind == 2 && !pendingRequest);
        EnableWindow(Control(ID_PROGRESS_RARE), kind == 2 && !pendingRequest);
        SetWindowTextA(Control(ID_PROGRESS_HINT), kind == 0 ? "Taticas: nivel recalcula experiencia e etapa automaticamente." :
            kind == 2 ? "Profissoes: nivel 0-100; progresso 0-9999 (0 no nivel 100); rare 0-4095." :
            kind == 1 ? "Habilidades / grandmasters: 0 remove, 1 aprende." : kind == 5 ? "Habilidades de combate: nivel 0-3." : kind == 6 ? "Progressoes adicionais: 0-32767. Etapa de magia religiosa: 0-5." : "Magias: 0 remove, 1 aprende. IDs correspondem ao catalogo do jogo.");
    }

    void FillProgressList()
    {
        const int kind = SelectedValue(ID_PROGRESS_KIND);
        const int count = kind == 0 ? 13 : kind == 2 ? 45 : kind == 5 ? 36 : kind == 6 ? 7 : kind == 1 ? 113 : 200;
        const char* extras[] = { "Ghost quest: nivel", "Ghost quest: conclusoes", "Fama dual (quest)", "Honor", "Honor maximo", "Faith", "Etapa de magia religiosa" };
        const char* tactics[] = { "Grapple", "Swordmanship", "Archery", "Fencing", "Mace fighting", "Pierce", "Whirl", "Hurl", "Parrying", "Double sword", "Double mace", "Magery", "Orison" };
        const char* combat[] = { "Lightning Boom", "Thunder Blow", "Lightning Shock", "Thunder Strike", "Lightning Shield", "Lightning Extreme", "Blood Earth", "Ground Attack", "Blood Will", "Stone Armor", "Simunian Bosom", "Earth Extreme", "Sword N Roses", "Multiple Fire", "Fleet Missile", "Converting Armor", "Burning Bloody", "Fire Extreme", "Poisoning Nova", "Cherroy Shade", "Dark Blade", "Tybern Gift", "Acquire Dark", "Dark Extreme", "Icing Blast", "Critical Hiding", "Shark Missile", "Horn of Iceberg", "Ice Shield", "Ice Extreme", "Charging", "Double Attack", "Whirlwind", "Twister", "Gust", "Wind Extreme" };
        SendMessage(Control(ID_PROGRESS_INDEX), CB_RESETCONTENT, 0, 0);
        for (int i = 0; i < count; ++i) {
            if (kind == 1 && i >= 88 && i <= 100) continue;
            const char* name = kind == 0 ? tactics[i] : kind == 5 ? combat[i] : kind == 6 ? extras[i] : "";
            if (kind == 1 || kind == 2) for (size_t j = 0; j < effects.size(); ++j)
                if (effects[j].dynamicEffect == (kind == 1 ? 2 : 3) && effects[j].id == i) { name = effects[j].name; break; }
            char label[140]; sprintf_s(label, "%03d - %.99s", i, name);
            LRESULT row = SendMessageA(Control(ID_PROGRESS_INDEX), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
            SendMessage(Control(ID_PROGRESS_INDEX), CB_SETITEMDATA, row, i);
        }
        SendMessage(Control(ID_PROGRESS_INDEX), CB_SETCURSEL, 0, 0);
        FillProgressValue();
    }

    void FillMonsters()
    {
        char filter[100] = {}; GetWindowTextA(Control(ID_MONSTER_SEARCH), filter, sizeof(filter));
        CharLowerBuffA(filter, static_cast<DWORD>(strlen(filter)));
        SendMessage(Control(ID_MONSTER), CB_RESETCONTENT, 0, 0);
        for (size_t i = 0; i < effects.size(); ++i) {
            if (effects[i].dynamicEffect != 4) continue;
            char label[140], lower[140]; sprintf_s(label, "%03d - %.99s", effects[i].id, effects[i].name);
            strcpy_s(lower, label); CharLowerBuffA(lower, static_cast<DWORD>(strlen(lower)));
            if (filter[0] && !strstr(lower, filter)) continue;
            LRESULT row = SendMessageA(Control(ID_MONSTER), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
            SendMessage(Control(ID_MONSTER), CB_SETITEMDATA, row, effects[i].id);
        }
        SendMessage(Control(ID_MONSTER), CB_SETCURSEL, 0, 0);
    }

    void SetBusy(bool busy)
    {
        const int buttons[] = { ID_REFRESH, ID_APPLY_STATS, ID_APPLY_GOLD, ID_APPLY_BANK, ID_APPLY_RE, ID_PREVIEW, ID_APPLY_DUAL, ID_APPLY_SYMBOL, ID_APPLY_PROGRESS, ID_SUMMON };
        for (int i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i)
            EnableWindow(Control(buttons[i]), !busy);
        EnableWindow(Control(ID_CREATE), !busy && previewToken != 0);
        EnableWindow(Control(ID_UNDO_STATS), !busy && canUndo);
        for (int id = ID_SEARCH; id <= ID_HIGH; ++id) EnableWindow(Control(id), !busy);
        for (int i = 0; i < LOCAL_ADMIN_ATTRIBUTE_COUNT; ++i) EnableWindow(Control(ID_ATTRIBUTE + i), !busy);
        const int fields[] = { ID_LEVEL, ID_EXP, ID_POINTS, ID_GOLD, ID_BANK, ID_RE };
        for (int i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) EnableWindow(Control(fields[i]), !busy);
        for (int id = ID_DUAL_CLASS; id <= ID_SYMBOL_GRADE; ++id) EnableWindow(Control(id), !busy);
        for (int id = ID_PROGRESS_KIND; id <= ID_PROGRESS_RARE; ++id) EnableWindow(Control(id), !busy);
        EnableWindow(Control(ID_PROGRESS_EXP), !busy && SelectedValue(ID_PROGRESS_KIND) == 2);
        EnableWindow(Control(ID_PROGRESS_RARE), !busy && SelectedValue(ID_PROGRESS_KIND) == 2);
        for (int id = ID_MONSTER_SEARCH; id <= ID_MONSTER_COUNT; ++id) EnableWindow(Control(id), !busy);
        EnableWindow(Control(ID_SUMMON), !busy && catalogReady);
        if (IsDlgButtonChecked(panel, ID_DYNAMIC) == BST_CHECKED)
        {
            EnableWindow(Control(ID_EFFECT2), FALSE);
            EnableWindow(Control(ID_EFFECT3), FALSE);
        }
    }

    void Status(const char* text) { SetWindowTextA(Control(ID_STATUS), text); }

    void SendRequest(LocalAdminRequest& request)
    {
        if (pendingRequest) return;
        if (!Hero || !SMenu[MN_MAININTERFACE].bActive)
        {
            Status("Entre no mapa com seu personagem para usar o painel.");
            return;
        }
        request.version = LOCAL_ADMIN_VERSION;
        request.requestId = ++nextRequest;
        if (!request.requestId) request.requestId = ++nextRequest;
        pendingRequest = request.requestId;
        if (request.operation == LA_READ)
        {
            catalogReady = false;
            catalogRequest = request.requestId;
        }
        t_packet packet = {};
        packet.h.header.type = CMD_LOCAL_ADMIN;
        packet.h.header.size = sizeof(request);
        memcpy(&packet.u, &request, sizeof(request));
        QueuePacket(&packet, 1);
        SetBusy(true);
        Status("Aguardando resposta do MapServer...");
        SetTimer(panel, 1, 10000, NULL);
    }

    void FillState(bool attributes)
    {
        fillingControls = true;
        if (attributes)
        {
            for (int i = 0; i < LOCAL_ADMIN_ATTRIBUTE_COUNT; ++i)
                SetNumber(ID_ATTRIBUTE + i, currentState.attributes[i]);
            SetNumber(ID_LEVEL, currentState.level);
            SetNumber(ID_EXP, currentState.experience);
            SetNumber(ID_POINTS, currentState.points);
        }
        SetNumber(ID_GOLD, currentState.gold);
        SetNumber(ID_BANK, currentState.bank);
        SetNumber(ID_RE, currentState.emblems);
        SendMessage(Control(ID_DUAL_CLASS), CB_SETCURSEL, currentState.dualClass, 0);
        SetNumber(ID_DUAL_STEP, currentState.dualStep); SetNumber(ID_FAME, currentState.fame);
        SetNumber(ID_LADDER, currentState.ladder); SetNumber(ID_CSP, currentState.combatPoints);
        SetNumber(ID_SYMBOL_GRADE, currentState.symbolGrade);
        char dualInfo[140]; sprintf_s(dualInfo, "Classe base: %d | Dual: %d | Symbol ID: %d, grau %d", currentState.baseClass, currentState.dualStep, currentState.symbolNumber, currentState.symbolGrade);
        SetWindowTextA(Control(ID_DUAL_INFO), dualInfo);
        FillProgressValue();
        fillingControls = false;
    }

    void InvalidatePreview()
    {
        if (fillingControls) return;
        previewToken = 0;
        EnableWindow(Control(ID_CREATE), FALSE);
        SetWindowTextA(Control(ID_PREVIEW_TEXT), "Configure o item e clique em Gerar previa.");
    }

    void FillItems()
    {
        char filter[100] = {};
        GetWindowTextA(Control(ID_SEARCH), filter, sizeof(filter));
        CharLowerBuffA(filter, static_cast<DWORD>(strlen(filter)));
        SendMessage(Control(ID_ITEMS), CB_RESETCONTENT, 0, 0);
        for (int type = 0; type <= 10; ++type)
        {
            for (int number = 1; number < Item_Ref.nItem[type] && number < ITEM_MAX; ++number)
            {
                CItem* item = ItemUnit(type, number);
                if (!item || item->GetItemId() != number || !item->GetItemHanName() || !item->GetItemHanName()[0]) continue;
                char label[180];
                sprintf_s(label, "%05d - %.120s", type * 1000 + number, item->GetItemHanName());
                char lower[180];
                strcpy_s(lower, label);
                CharLowerBuffA(lower, static_cast<DWORD>(strlen(lower)));
                if (filter[0] && !strstr(lower, filter)) continue;
                const LRESULT index = SendMessageA(Control(ID_ITEMS), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
                SendMessage(Control(ID_ITEMS), CB_SETITEMDATA, index, type * 1000 + number);
            }
        }
        SendMessage(Control(ID_ITEMS), CB_SETCURSEL, 0, 0);
        InvalidatePreview();
    }

    void FillEffects()
    {
        const bool dynamic = IsDlgButtonChecked(panel, ID_DYNAMIC) == BST_CHECKED;
        for (int control = ID_EFFECT1; control <= ID_EFFECT3; ++control)
        {
            HWND combo = Control(control);
            SendMessage(combo, CB_RESETCONTENT, 0, 0);
            SendMessageA(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("Nenhum"));
            SendMessage(combo, CB_SETITEMDATA, 0, 0);
            for (size_t i = 0; i < effects.size(); ++i)
            {
                if (effects[i].dynamicEffect != (dynamic ? 1 : 0)) continue;
                char label[140];
                sprintf_s(label, "%d - %.99s", effects[i].id, effects[i].name);
                const LRESULT index = SendMessageA(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
                SendMessage(combo, CB_SETITEMDATA, index, effects[i].id);
            }
            SendMessage(combo, CB_SETCURSEL, 0, 0);
            EnableWindow(combo, !dynamic || control == ID_EFFECT1);
        }
        InvalidatePreview();
    }

    void ShowPage(int selected)
    {
        for (int page = 0; page < 6; ++page)
            for (size_t i = 0; i < pages[page].size(); ++i)
                ShowWindow(pages[page][i], page == selected ? SW_SHOW : SW_HIDE);
    }

    void BuildControls()
    {
        tabControl = AddControl(WC_TABCONTROLA, "", WS_TABSTOP, ID_TABS, 12, 12, 730, 32);
        const char* names[] = { "Personagem", "Criar itens", "Dinheiro e RE", "Dual / Symbol", "Progressoes", "Monstros" };
        for (int i = 0; i < 6; ++i)
        {
            TCITEMA tab = {};
            tab.mask = TCIF_TEXT;
            tab.pszText = const_cast<char*>(names[i]);
            SendMessageA(tabControl, TCM_INSERTITEMA, i, reinterpret_cast<LPARAM>(&tab));
        }
        for (int i = 0; i < LOCAL_ADMIN_ATTRIBUTE_COUNT; ++i)
        {
            const int x = 30 + (i / 6) * 225;
            const int y = 70 + (i % 6) * 43;
            AddControl("STATIC", attributeNames[i], 0, 0, x, y + 4, 65, 20, 0);
            AddControl("EDIT", "0", ES_NUMBER | WS_TABSTOP, ID_ATTRIBUTE + i, x + 75, y, 105, 25, 0);
        }
        const char* levelLabels[] = { "Nivel", "Experiencia", "Pontos livres" };
        const int levelIds[] = { ID_LEVEL, ID_EXP, ID_POINTS };
        for (int i = 0; i < 3; ++i)
        {
            AddControl("STATIC", levelLabels[i], 0, 0, 495, 75 + i * 65, 180, 20, 0);
            AddControl("EDIT", "0", ES_NUMBER | WS_TABSTOP, levelIds[i], 495, 98 + i * 65, 180, 25, 0);
        }
        AddControl("BUTTON", "Aplicar atributos", WS_TABSTOP, ID_APPLY_STATS, 30, 365, 170, 32, 0);
        AddControl("BUTTON", "Desfazer atributos", WS_TABSTOP, ID_UNDO_STATS, 215, 365, 170, 32, 0);
        AddControl("STATIC", "STR, DEX, CON e demais valores sao aplicados no personagem conectado.", 0, 0, 30, 420, 660, 36, 0);

        AddControl("STATIC", "Buscar por nome ou ID", 0, 0, 30, 65, 210, 20, 1);
        AddControl("EDIT", "", ES_AUTOHSCROLL | WS_TABSTOP, ID_SEARCH, 30, 88, 310, 25, 1);
        AddControl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_ITEMS, 30, 125, 665, 300, 1);
        const char* itemLabels[] = { "Quantidade", "Durabilidade (0 = padrao)", "Grau (0 = normal, 1-15 = raro)" };
        const int itemIds[] = { ID_QUANTITY, ID_DURABILITY, ID_GRADE };
        for (int i = 0; i < 3; ++i)
        {
            AddControl("STATIC", itemLabels[i], 0, 0, 30 + i * 225, 171, 220, 20, 1);
            AddControl("EDIT", i == 0 ? "1" : "0", ES_NUMBER | WS_TABSTOP, itemIds[i], 30 + i * 225, 195, 110, 25, 1);
        }
        AddControl("BUTTON", "Efeito dinamico (consumivel)", BS_AUTOCHECKBOX | WS_TABSTOP, ID_DYNAMIC, 30, 237, 250, 24, 1);
        AddControl("BUTTON", "Item high", BS_AUTOCHECKBOX | WS_TABSTOP, ID_HIGH, 315, 237, 150, 24, 1);
        for (int i = 0; i < 3; ++i)
            AddControl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_EFFECT1 + i, 30, 275 + i * 35, 325, 230, 1);
        AddControl("EDIT", "Configure o item e clique em Gerar previa.", ES_MULTILINE | ES_READONLY | WS_VSCROLL, ID_PREVIEW_TEXT, 380, 275, 315, 155, 1);
        AddControl("BUTTON", "Gerar previa", WS_TABSTOP, ID_PREVIEW, 30, 397, 150, 32, 1);
        AddControl("BUTTON", "Adicionar ao inventario", WS_TABSTOP, ID_CREATE, 30, 445, 245, 32, 1);

        const char* currencyLabels[] = { "Ouro no inventario", "Saldo no banco", "RE - Raja Emblems no inventario" };
        for (int i = 0; i < 3; ++i)
        {
            AddControl("STATIC", currencyLabels[i], 0, 0, 35, 90 + i * 105, 400, 22, 2);
            AddControl("EDIT", "0", ES_NUMBER | WS_TABSTOP, ID_GOLD + i, 35, 118 + i * 105, 250, 27, 2);
            AddControl("BUTTON", "Aplicar saldo", WS_TABSTOP, ID_APPLY_GOLD + i, 315, 116 + i * 105, 160, 32, 2);
        }
        AddControl("STATIC", "Informe o saldo final desejado. RE usa o item 10057 do jogo.", 0, 0, 35, 440, 650, 40, 2);
        AddControl("STATIC", "Classe dual (a classe base e preservada)", 0, 0, 30, 65, 400, 22, 3);
        AddControl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_TABSTOP, ID_DUAL_CLASS, 30, 90, 290, 180, 3);
        const char* classes[] = { "0 - Warrior", "1 - Thief", "2 - Archer", "3 - Wizard", "4 - Priest" };
        for (int i = 0; i < 5; ++i) { SendMessageA(Control(ID_DUAL_CLASS), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(classes[i])); SendMessage(Control(ID_DUAL_CLASS), CB_SETITEMDATA, i, i); }
        const char* dualLabels[] = { "Etapa dual (0-5)", "Fama (0-32767)", "Ladder", "Pontos combate (0-127)", "Grau symbol (0 remove, 1-5 cria)" };
        for (int i = 0; i < 5; ++i) {
            AddControl("STATIC", dualLabels[i], 0, 0, 30, 135 + i * 51, 330, 22, 3);
            AddControl("EDIT", "0", ES_NUMBER | WS_TABSTOP, ID_DUAL_STEP + i, 380, 132 + i * 51, 180, 25, 3);
        }
        AddControl("BUTTON", "Aplicar dual, fama, ladder e CSP", WS_TABSTOP, ID_APPLY_DUAL, 30, 410, 310, 32, 3);
        AddControl("BUTTON", "Aplicar dual symbol", WS_TABSTOP, ID_APPLY_SYMBOL, 380, 410, 230, 32, 3);
        AddControl("STATIC", "", 0, ID_DUAL_INFO, 30, 465, 680, 30, 3);
        AddControl("STATIC", "Categoria", 0, 0, 30, 65, 600, 22, 4);
        AddControl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_TABSTOP, ID_PROGRESS_KIND, 30, 90, 650, 200, 4);
        const char* categories[] = { "Taticas", "Habilidades e grandmasters", "Profissoes / fabricacao", "Magias Wizard (IDs)", "Magias Priest (IDs)", "Habilidades de combate", "Ghost, honor, fama dual e religiao" };
        for (int i = 0; i < 7; ++i) { SendMessageA(Control(ID_PROGRESS_KIND), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(categories[i])); SendMessage(Control(ID_PROGRESS_KIND), CB_SETITEMDATA, i, i); }
        SendMessage(Control(ID_PROGRESS_KIND), CB_SETCURSEL, 0, 0);
        AddControl("STATIC", "Habilidade / ID", 0, 0, 30, 140, 650, 22, 4);
        AddControl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_PROGRESS_INDEX, 30, 165, 650, 260, 4);
        const char* progressLabels[] = { "Nivel / aprendido", "Progresso profissao", "Rare profissao" };
        for (int i = 0; i < 3; ++i) {
            AddControl("STATIC", progressLabels[i], 0, 0, 30 + i * 225, 220, 215, 22, 4);
            AddControl("EDIT", "0", ES_NUMBER | WS_TABSTOP, ID_PROGRESS_LEVEL + i, 30 + i * 225, 247, 190, 25, 4);
        }
        AddControl("STATIC", "", 0, ID_PROGRESS_HINT, 30, 300, 650, 65, 4);
        AddControl("BUTTON", "Aplicar progressao selecionada", WS_TABSTOP, ID_APPLY_PROGRESS, 30, 390, 320, 32, 4);
        FillProgressList();
        AddControl("STATIC", "Buscar monstro por nome ou ID", 0, 0, 30, 65, 650, 22, 5);
        AddControl("EDIT", "", ES_AUTOHSCROLL | WS_TABSTOP, ID_MONSTER_SEARCH, 30, 93, 650, 25, 5);
        AddControl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_MONSTER, 30, 145, 650, 280, 5);
        AddControl("STATIC", "Quantidade (1-20)", 0, 0, 30, 200, 300, 22, 5);
        AddControl("EDIT", "1", ES_NUMBER | WS_TABSTOP, ID_MONSTER_COUNT, 30, 230, 150, 25, 5);
        AddControl("STATIC", "Cria monstros livres perto do personagem, no mapa atual.\r\nUsa locais transitaveis e nao ativa respawn automatico.\r\nIntervalo minimo entre comandos: 3 segundos.", 0, 0, 30, 290, 650, 80, 5);
        AddControl("BUTTON", "Invocar monstros", WS_TABSTOP, ID_SUMMON, 30, 400, 260, 32, 5);
        AddControl("BUTTON", "Atualizar dados", WS_TABSTOP, ID_REFRESH, 30, 510, 160, 30);
        AddControl("STATIC", "Conectando ao servidor...", 0, ID_STATUS, 210, 508, 500, 46);
        FillItems();
        FillEffects();
        ShowPage(0);
        SetBusy(false);
    }

    void OnCommand(int id, int notification)
    {
        if (id == ID_MONSTER_SEARCH && notification == EN_CHANGE) { FillMonsters(); return; }
        if (id == ID_PROGRESS_KIND && notification == CBN_SELCHANGE) { FillProgressList(); return; }
        if (id == ID_PROGRESS_INDEX && notification == CBN_SELCHANGE) { FillProgressValue(); return; }
        if (id == ID_SEARCH && notification == EN_CHANGE) { FillItems(); return; }
        if (id == ID_DYNAMIC && notification == BN_CLICKED) { FillEffects(); return; }
        if ((id >= ID_ITEMS && id <= ID_HIGH) && (notification == EN_CHANGE || notification == CBN_SELCHANGE || notification == BN_CLICKED))
            InvalidatePreview();
        if (notification != BN_CLICKED || pendingRequest) return;
        LocalAdminRequest request = {};
        request.state = currentState;
        DWORD value = 0;
        switch (id)
        {
        case ID_SUMMON:
            if (!catalogReady || SendMessage(Control(ID_MONSTER), CB_GETCURSEL, 0, 0) == CB_ERR) { Status("Selecione um monstro do catalogo do servidor."); return; }
            request.operation = LA_SUMMON; request.itemNumber = SelectedValue(ID_MONSTER);
            if (!ReadNumber(ID_MONSTER_COUNT, 20, value) || !value) { Status("Quantidade: 1-20."); return; }
            request.quantity = value;
            break;
        case ID_APPLY_DUAL:
            request.operation = LA_DUAL; request.state.dualClass = SelectedValue(ID_DUAL_CLASS);
            if (!ReadNumber(ID_DUAL_STEP, 5, value)) { Status("Etapa dual: 0-5."); return; } request.state.dualStep = value;
            if (!ReadNumber(ID_FAME, 32767, value)) { Status("Fama: 0-32767."); return; } request.state.fame = value;
            if (!ReadNumber(ID_LADDER, INT_MAX, value)) { Status("Ladder fora do limite."); return; } request.state.ladder = value;
            if (!ReadNumber(ID_CSP, 127, value)) { Status("Pontos combate: 0-127."); return; } request.state.combatPoints = value;
            break;
        case ID_APPLY_SYMBOL:
            request.operation = LA_SYMBOL;
            if (!ReadNumber(ID_SYMBOL_GRADE, 5, value)) { Status("Symbol: 0 remove, 1-5 cria."); return; } request.grade = value;
            break;
        case ID_APPLY_PROGRESS:
            request.operation = LA_PROGRESSION;
            request.progressionKind = SelectedValue(ID_PROGRESS_KIND); request.progressionIndex = SelectedValue(ID_PROGRESS_INDEX);
            if (!ReadNumber(ID_PROGRESS_LEVEL, 32767, value)) { Status("Nivel fora do limite."); return; } request.progressionLevel = value;
            if (!ReadNumber(ID_PROGRESS_EXP, 9999, value)) { Status("Progresso: 0-9999."); return; } request.progressionExp = value;
            if (!ReadNumber(ID_PROGRESS_RARE, 4095, value)) { Status("Rare profissao: 0-4095."); return; } request.progressionRare = value;
            break;
        case ID_REFRESH: request.operation = LA_READ; previewToken = 0; break;
        case ID_UNDO_STATS: request.operation = LA_UNDO_ATTRIBUTES; break;
        case ID_APPLY_STATS:
            request.operation = LA_ATTRIBUTES;
            for (int i = 0; i < LOCAL_ADMIN_ATTRIBUTE_COUNT; ++i)
            {
                if (!ReadNumber(ID_ATTRIBUTE + i, 32767, value)) { Status("Atributos devem ser numeros de 0 a 32767."); return; }
                request.state.attributes[i] = value;
            }
            if (!ReadNumber(ID_LEVEL, 255, value) || !value) { Status("Nivel invalido."); return; }
            request.state.level = value;
            if (!ReadNumber(ID_EXP, UINT_MAX, value)) { Status("Experiencia invalida."); return; }
            request.state.experience = value;
            if (!ReadNumber(ID_POINTS, 32767, value)) { Status("Pontos livres invalidos."); return; }
            request.state.points = value;
            break;
        case ID_APPLY_GOLD:
        case ID_APPLY_BANK:
        case ID_APPLY_RE:
            if (!ReadNumber(ID_GOLD + id - ID_APPLY_GOLD, id == ID_APPLY_BANK ? 4000000000U : id == ID_APPLY_RE ? 1000000000U : 2000000000U, value))
            { Status("Saldo invalido ou acima do limite suportado."); return; }
            request.operation = id == ID_APPLY_GOLD ? LA_GOLD : id == ID_APPLY_BANK ? LA_BANK : LA_EMBLEMS;
            if (id == ID_APPLY_GOLD) request.state.gold = value;
            if (id == ID_APPLY_BANK) request.state.bank = value;
            if (id == ID_APPLY_RE) request.state.emblems = value;
            break;
        case ID_PREVIEW:
            request.operation = LA_PREVIEW;
            request.itemNumber = SelectedValue(ID_ITEMS);
            if (!request.itemNumber || !ReadNumber(ID_QUANTITY, 1000000, value) || !value) { Status("Selecione um item e uma quantidade valida."); return; }
            request.quantity = value;
            if (!ReadNumber(ID_DURABILITY, 65535, value)) { Status("Durabilidade: 0 a 65535."); return; }
            request.durability = value;
            if (!ReadNumber(ID_GRADE, 15, value)) { Status("Grau: 0 a 15."); return; }
            request.grade = value;
            if (request.grade && !catalogReady) { Status("Aguarde o catalogo de efeitos do servidor."); return; }
            for (int i = 0; i < 3; ++i) request.effects[i] = SelectedValue(ID_EFFECT1 + i);
            request.dynamicEffect = IsDlgButtonChecked(panel, ID_DYNAMIC) == BST_CHECKED;
            request.highItem = IsDlgButtonChecked(panel, ID_HIGH) == BST_CHECKED;
            break;
        case ID_CREATE:
            request.operation = LA_CREATE;
            request.previewToken = previewToken;
            break;
        default: return;
        }
        SendRequest(request);
    }

    LRESULT CALLBACK PanelProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_CREATE: panel = window; BuildControls(); return 0;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDCANCEL) { ShowWindow(window, SW_HIDE); SetForegroundWindow(g_hwndMain); return 0; }
            OnCommand(LOWORD(wParam), HIWORD(wParam)); return 0;
        case WM_NOTIFY:
            if (reinterpret_cast<NMHDR*>(lParam)->idFrom == ID_TABS && reinterpret_cast<NMHDR*>(lParam)->code == TCN_SELCHANGE)
                ShowPage(TabCtrl_GetCurSel(tabControl));
            return 0;
        case WM_TIMER:
            KillTimer(window, 1);
            pendingRequest = 0;
            previewToken = 0;
            SetBusy(false);
            Status("Sem resposta. Atualize os dados antes de repetir a operacao.");
            return 0;
        case WM_CLOSE: ShowWindow(window, SW_HIDE); SetForegroundWindow(g_hwndMain); return 0;
        }
        return DefWindowProcA(window, message, wParam, lParam);
    }
}

void ToggleLocalAdminPanel()
{
    if (!GetPrivateProfileIntA("local", "AdminPanel", 0, ".\\dragon.ini")) return;
    if (!Hero || !SMenu[MN_MAININTERFACE].bActive) return;
    if (!panel)
    {
        INITCOMMONCONTROLSEX controls = { sizeof(controls), ICC_TAB_CLASSES };
        InitCommonControlsEx(&controls);
        WNDCLASSA windowClass = {};
        windowClass.lpfnWndProc = PanelProcedure;
        windowClass.hInstance = g_hInstance;
        windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        windowClass.lpszClassName = "DragonRajaLocalAdmin";
        RegisterClassA(&windowClass);
        RECT game;
        GetWindowRect(g_hwndMain, &game);
        MONITORINFO monitor = { sizeof(monitor) };
        GetMonitorInfo(MonitorFromWindow(g_hwndMain, MONITOR_DEFAULTTONEAREST), &monitor);
        int left = game.left + 35;
        int top = game.top + 45;
        if (left + 770 > monitor.rcWork.right) left = monitor.rcWork.right - 770;
        if (top + 600 > monitor.rcWork.bottom) top = monitor.rcWork.bottom - 600;
        if (left < monitor.rcWork.left) left = monitor.rcWork.left;
        if (top < monitor.rcWork.top) top = monitor.rcWork.top;
        CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT, windowClass.lpszClassName, "Painel GM local",
            WS_POPUP | WS_CAPTION | WS_SYSMENU, left, top,
            770, 600, g_hwndMain, NULL, g_hInstance, NULL);
    }
    if (IsWindowVisible(panel)) { ShowWindow(panel, SW_HIDE); return; }
    ShowWindow(panel, SW_SHOW);
    SetForegroundWindow(panel);
    pendingRequest = 0;
    previewToken = 0;
    FillItems();
    LocalAdminRequest request = {};
    request.operation = LA_READ;
    SendRequest(request);
}

void ReceiveLocalAdminReply(const void* payload, int length)
{
    if (length != sizeof(LocalAdminReply)) return;
    LocalAdminReply reply;
    memcpy(&reply, payload, sizeof(reply));
    if (!panel || reply.version != LOCAL_ADMIN_VERSION || reply.requestId != pendingRequest) return;
    reply.message[sizeof(reply.message) - 1] = 0;
    reply.state.character[sizeof(reply.state.character) - 1] = 0;
    KillTimer(panel, 1);
    pendingRequest = 0;
    canUndo = reply.canUndo != 0;
    if (reply.success)
    {
        currentState = reply.state;
        if (Hero) {
            Hero->aStepInfo[CLS_STEP] = static_cast<char>(currentState.dualStep);
            Hero->aStepInfo[DUAL_CLS] = static_cast<char>(currentState.dualClass);
            Hero->aStepInfo[CSP_STEP] = static_cast<char>(currentState.combatPoints);
        }
        memcpy(SCharacterData.SkillId, currentState.skills, sizeof(currentState.skills));
        memcpy(SCharacterData.MagicId, SCharacterData.nCharacterData[SPELL] == PRIEST_SPELL ? currentState.priest : currentState.wizard, 200);
        SCharacterData.faith = currentState.extraProgress[5];
        if (reply.operation == LA_PROGRESSION) {
            ReadMagicTable();
            ResetMagicQuickArray();
            if (SCharacterData.nCharacterData[SPELL] == PRIEST_SPELL) MagicSetting2();
        }
        for (int i = 0; i < 45; ++i) {
            SCharacterData.nJobSkill[i].skillexp = currentState.workExp[i];
            SCharacterData.nJobSkill[i].rare = currentState.workRare[i];
        }
        g_SymBolMgr.SetDualItem(currentState.symbolNumber, currentState.symbolGrade);
        SCharacterData.nMoney = currentState.gold;
        SCharacterData.BankMoney = currentState.bank;
        if (reply.operation == LA_READ || reply.operation == LA_ATTRIBUTES || reply.operation == LA_UNDO_ATTRIBUTES)
        {
            const int kinds[] = { STR, DEX, CON, WIS, INT_, CHA, MOVP, ENDU, MOR, LUCK, WSPS };
            for (int i = 0; i < LOCAL_ADMIN_ATTRIBUTE_COUNT; ++i)
                SCharacterData.nCharacterAbility[kinds[i]] = currentState.attributes[i];
            SCharacterData.nLevel = static_cast<short>(currentState.level);
            SCharacterData.nExp = currentState.experience;
            SCharacterData.LvUpPoint = currentState.points;
        }
        FillState(reply.operation == LA_READ || reply.operation == LA_ATTRIBUTES || reply.operation == LA_UNDO_ATTRIBUTES);
        if (reply.operation == LA_PREVIEW)
        {
            previewToken = reply.previewToken;
            SetWindowTextA(Control(ID_PREVIEW_TEXT), reply.message);
        }
        if (reply.operation == LA_CREATE) previewToken = 0;
    }
    Status(reply.message);
    SetBusy(false);
}

void ReceiveLocalAdminCatalog(const void* payload, int length)
{
    if (length != sizeof(LocalAdminCatalog)) return;
    LocalAdminCatalog catalog;
    memcpy(&catalog, payload, sizeof(catalog));
    if (!panel || catalog.version != LOCAL_ADMIN_VERSION || catalog.requestId != catalogRequest || catalog.count > 24) return;
    if (catalog.first) effects.clear();
    for (DWORD i = 0; i < catalog.count; ++i)
    {
        catalog.effects[i].name[99] = 0;
        effects.push_back(catalog.effects[i]);
    }
    if (catalog.last)
    {
        catalogReady = true;
        FillEffects();
        FillProgressList();
        FillMonsters();
        SetBusy(pendingRequest != 0);
    }
}

bool HandleLocalAdminMessage(MSG* message)
{
    return panel && IsWindowVisible(panel) && IsDialogMessage(panel, message);
}

