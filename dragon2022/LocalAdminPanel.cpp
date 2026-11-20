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
#include "CurrentMsgMgr.h"
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
static_assert(sizeof(LocalAdminReply) <= MAX_PACKET_SIZE, "Reply exceeds client packet limit");
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
        ID_MONSTER_SEARCH = 4140, ID_MONSTER, ID_MONSTER_COUNT, ID_SUMMON,
        ID_LEGEND = 4150, ID_MAP_INFO, ID_MAP_X, ID_MAP_Y, ID_TELEPORT, ID_MARK, ID_RETURN,
        ID_SESSION, ID_ANNOUNCEMENT, ID_ANNOUNCE, ID_GATHER, ID_HISTORY,
        ID_SUMMON_LIST, ID_SUMMON_GROUP, ID_SUMMON_REMOVE, ID_WAVE_COUNT, ID_WAVE_DELAY, ID_WAVE_STOP,
        ID_SAVE_PRESET, ID_APPLY_PRESET, ID_MAP_CODE, ID_MAP_SEARCH, ID_UNDO_TACTIC,
        ID_ADV_TARGET, ID_INSPECT, ID_RECOVER_HP, ID_RECOVER_CURSE, ID_REVIVE, ID_TRAINING,
        ID_TRAINING_STATS, ID_SESSION_ALL, ID_ANNOUNCE_ALL, ID_AUDIT, ID_ADV_DETAILS,
        ID_LEGEND_VARIANT, ID_LEGEND_CATALOG, ID_EVENT_NAME, ID_EVENT_SAVE, ID_EVENT_RUN, ID_EVENT_END
    };

    HWND panel = NULL;
    HWND lifeWindow = NULL;
    HWND lifeButton = NULL;
    HWND tabControl = NULL;
    std::vector<HWND> pages[9];
    DWORD activeEventGroup = 0;
    LocalAdminState activeEventLocation = {};
    const char* eventFile = ".\\gm-events.ini";
    LocalAdminState returnPoint = {};
    LocalAdminState preset = {};
    LocalAdminRequest waveRequest = {};
    int wavesRemaining = 0;
    DWORD waveDelay = 0;
    LocalAdminState currentState = {};
    DWORD nextRequest = 0;
    DWORD pendingRequest = 0;
    bool markOnReply = false;
    bool capturePreset = false;
    bool eventReply = false;
    DWORD pendingOperation = 0;
    DWORD previewToken = 0;
    bool canUndo = false;
    bool fillingControls = false;
    bool catalogReady = false;
    DWORD catalogRequest = 0;
    DWORD rosterRequest = 0;
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
        if (kind == 0 && index >= 0 && index < 13) level = currentState.skills[88 + index];
        if (kind == 1 && index < 200) level = currentState.skills[index];
        if (kind == 2 && index < 45) { level = currentState.workExp[index] / 10000; exp = currentState.workExp[index] % 10000; rare = currentState.workRare[index]; }
        if (kind == 3 && index < 200) level = currentState.wizard[index];
        if (kind == 4 && index < 200) level = currentState.priest[index];
        if (kind == 5 && index < 36) level = currentState.skills[115 + index];
        if (kind == 6 && index < 7) level = currentState.extraProgress[index];
        SetNumber(ID_PROGRESS_LEVEL, level); SetNumber(ID_PROGRESS_EXP, exp); SetNumber(ID_PROGRESS_RARE, rare);
        EnableWindow(Control(ID_PROGRESS_EXP), kind == 2 && !pendingRequest);
        EnableWindow(Control(ID_PROGRESS_RARE), kind == 2 && !pendingRequest);
        char tactic[130] = "Selecione uma tatica para consultar o servidor.";
        if (kind == 0 && index >= 0 && index < 13)
            sprintf_s(tactic, "Servidor: nivel %u | XP %lu | etapa %d. Atualize os dados para consultar novamente.",
                static_cast<unsigned>(currentState.skills[88 + index]),
                static_cast<unsigned long>(currentState.tacticExp[index]), int(currentState.tacticStep[index]));
        SetWindowTextA(Control(ID_PROGRESS_HINT), kind == 0 ? tactic :
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
        const int buttons[] = { ID_REFRESH, ID_APPLY_STATS, ID_APPLY_GOLD, ID_APPLY_BANK, ID_APPLY_RE, ID_PREVIEW, ID_APPLY_DUAL, ID_APPLY_SYMBOL, ID_APPLY_PROGRESS, ID_SUMMON, ID_TELEPORT, ID_MARK, ID_RETURN, ID_SESSION, ID_ANNOUNCE, ID_GATHER, ID_SUMMON_LIST, ID_SUMMON_REMOVE, ID_SAVE_PRESET, ID_APPLY_PRESET, ID_MAP_SEARCH, ID_UNDO_TACTIC,
            ID_INSPECT, ID_RECOVER_HP, ID_RECOVER_CURSE, ID_REVIVE, ID_TRAINING, ID_TRAINING_STATS,
            ID_SESSION_ALL, ID_ANNOUNCE_ALL, ID_AUDIT, ID_LEGEND_CATALOG, ID_EVENT_SAVE, ID_EVENT_RUN, ID_EVENT_END };
        for (int i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i)
            EnableWindow(Control(buttons[i]), !busy);
        EnableWindow(Control(ID_CREATE), !busy);
        if (lifeButton) EnableWindow(lifeButton, !busy);
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
        EnableWindow(Control(ID_LEGEND), !busy);
        EnableWindow(Control(ID_LEGEND_VARIANT), !busy && IsDlgButtonChecked(panel, ID_LEGEND) == BST_CHECKED);
        EnableWindow(Control(ID_EVENT_END), !busy && activeEventGroup != 0);
        EnableWindow(Control(ID_RETURN), !busy && returnPoint.map[0]);
        EnableWindow(Control(ID_APPLY_PRESET), !busy && preset.character[0]);
        EnableWindow(Control(ID_WAVE_STOP), wavesRemaining > 0);
        if (IsDlgButtonChecked(panel, ID_LEGEND) == BST_CHECKED)
        {
            for (int id = ID_EFFECT1; id <= ID_HIGH; ++id) EnableWindow(Control(id), FALSE);
            EnableWindow(Control(ID_GRADE), !busy);
        }
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
        rosterRequest = request.operation == LA_SESSION_ALL ? request.requestId : 0;
        markOnReply = request.operation == LA_SESSION && request.progressionIndex == 1;
        capturePreset = request.operation == LA_SESSION && request.progressionIndex == 2;
        eventReply = request.progressionIndex == 3 && (request.operation == LA_SUMMON || request.operation == LA_SUMMON_REMOVE);
        pendingOperation = request.operation;
        if (request.operation == LA_READ)
        {
            catalogReady = false;
            catalogRequest = request.requestId;
        }
        t_packet packet = {};
        packet.h.header.type = CMD_LOCAL_ADMIN;
        packet.h.header.size = sizeof(request);
        memcpy(&packet.u, &request, sizeof(request));
        if (!QueuePacket(&packet, 1))
        {
            pendingRequest = 0;
            Status("Nao foi possivel enfileirar o comando. Verifique a conexao.");
            return;
        }
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
        EnableWindow(Control(ID_CREATE), !pendingRequest);
        SetWindowTextA(Control(ID_PREVIEW_TEXT), "Configure o item e adicione ao inventario. A previa e opcional.");
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
        for (int page = 0; page < 9; ++page)
            for (size_t i = 0; i < pages[page].size(); ++i)
                ShowWindow(pages[page][i], page == selected ? SW_SHOW : SW_HIDE);
    }

    void BuildControls()
    {
        tabControl = AddControl(WC_TABCONTROLA, "", WS_TABSTOP, ID_TABS, 12, 12, 730, 32);
        const char* names[] = { "Personagem", "Criar itens", "Dinheiro e RE", "Dual / Symbol", "Progressoes", "Monstros", "Mapa / Sessao", "Historico", "Ferramentas" };
        for (int i = 0; i < 9; ++i)
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
        AddControl("BUTTON", "Legend", BS_AUTOCHECKBOX | WS_TABSTOP, ID_LEGEND, 470, 237, 220, 24, 1);
        for (int i = 0; i < 3; ++i)
            AddControl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_EFFECT1 + i, 30, 275 + i * 35, 325, 230, 1);
        AddControl("EDIT", "Configure o item e adicione ao inventario. A previa e opcional.", ES_MULTILINE | ES_READONLY | WS_VSCROLL, ID_PREVIEW_TEXT, 380, 275, 315, 155, 1);
        AddControl("BUTTON", "Gerar previa", WS_TABSTOP, ID_PREVIEW, 30, 397, 150, 32, 1);
        AddControl("BUTTON", "Adicionar ao inventario", WS_TABSTOP, ID_CREATE, 30, 445, 245, 32, 1);
        AddControl("STATIC", "Variante Legend (0 = sorteio)", 0, 0, 380, 437, 220, 20, 1);
        AddControl("EDIT", "0", ES_NUMBER | WS_TABSTOP, ID_LEGEND_VARIANT, 380, 459, 70, 23, 1);
        AddControl("BUTTON", "Consultar variantes", WS_TABSTOP, ID_LEGEND_CATALOG, 465, 455, 220, 30, 1);

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
        AddControl("BUTTON", "Desfazer ultima tatica", WS_TABSTOP, ID_UNDO_TACTIC, 375, 390, 210, 32, 4);
        AddControl("BUTTON", "Capturar preset", WS_TABSTOP, ID_SAVE_PRESET, 30, 445, 180, 32, 4);
        AddControl("BUTTON", "Reaplicar preset", WS_TABSTOP, ID_APPLY_PRESET, 225, 445, 180, 32, 4);
        FillProgressList();
        AddControl("STATIC", "Buscar monstro por nome ou ID", 0, 0, 30, 65, 650, 22, 5);
        AddControl("EDIT", "", ES_AUTOHSCROLL | WS_TABSTOP, ID_MONSTER_SEARCH, 30, 93, 650, 25, 5);
        AddControl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_MONSTER, 30, 145, 650, 280, 5);
        AddControl("STATIC", "Quantidade (1-20)", 0, 0, 30, 200, 180, 22, 5);
        AddControl("EDIT", "1", ES_NUMBER | WS_TABSTOP, ID_MONSTER_COUNT, 30, 230, 150, 25, 5);
        AddControl("STATIC", "Ondas (1-5)", 0, 0, 210, 200, 170, 22, 5);
        AddControl("EDIT", "1", ES_NUMBER | WS_TABSTOP, ID_WAVE_COUNT, 210, 230, 150, 25, 5);
        AddControl("STATIC", "Intervalo em segundos (3-60)", 0, 0, 390, 200, 290, 22, 5);
        AddControl("EDIT", "5", ES_NUMBER | WS_TABSTOP, ID_WAVE_DELAY, 390, 230, 150, 25, 5);
        AddControl("BUTTON", "Invocar monstros", WS_TABSTOP, ID_SUMMON, 30, 290, 190, 32, 5);
        AddControl("BUTTON", "Parar ondas", WS_TABSTOP, ID_WAVE_STOP, 235, 290, 160, 32, 5);
        AddControl("BUTTON", "Listar grupos ativos", WS_TABSTOP, ID_SUMMON_LIST, 30, 355, 190, 32, 5);
        AddControl("STATIC", "ID do grupo", 0, 0, 30, 420, 100, 25, 5);
        AddControl("EDIT", "", ES_NUMBER | WS_TABSTOP, ID_SUMMON_GROUP, 130, 417, 120, 25, 5);
        AddControl("BUTTON", "Remover grupo", WS_TABSTOP, ID_SUMMON_REMOVE, 265, 414, 170, 32, 5);
        AddControl("STATIC", "Mapa e coordenadas do servidor", 0, ID_MAP_INFO, 30, 68, 650, 22, 6);
        AddControl("STATIC", "X / Y", 0, 0, 30, 105, 50, 25, 6);
        AddControl("EDIT", "0", ES_NUMBER | WS_TABSTOP, ID_MAP_X, 85, 102, 95, 25, 6);
        AddControl("EDIT", "0", ES_NUMBER | WS_TABSTOP, ID_MAP_Y, 195, 102, 95, 25, 6);
        AddControl("BUTTON", "Teleportar", WS_TABSTOP, ID_TELEPORT, 310, 99, 220, 32, 6);
        AddControl("BUTTON", "Marcar posicao atual", WS_TABSTOP, ID_MARK, 30, 150, 200, 32, 6);
        AddControl("BUTTON", "Voltar ao ponto marcado", WS_TABSTOP, ID_RETURN, 245, 150, 220, 32, 6);
        AddControl("BUTTON", "Jogadores neste mapa", WS_TABSTOP, ID_SESSION, 30, 230, 210, 32, 6);
        AddControl("BUTTON", "Reunir jogadores aqui", WS_TABSTOP, ID_GATHER, 255, 230, 230, 32, 6);
        AddControl("EDIT", "", ES_AUTOHSCROLL | WS_TABSTOP, ID_ANNOUNCEMENT, 30, 293, 460, 25, 6);
        SendMessage(Control(ID_ANNOUNCEMENT), EM_SETLIMITTEXT, 119, 0);
        AddControl("BUTTON", "Enviar aviso ao mapa", WS_TABSTOP, ID_ANNOUNCE, 505, 290, 190, 32, 6);
        AddControl("STATIC", "Codigo do mapa / busca por nome", 0, 0, 30, 352, 460, 22, 6);
        AddControl("EDIT", "", ES_AUTOHSCROLL | WS_TABSTOP, ID_MAP_CODE, 30, 380, 460, 25, 6);
        SendMessage(Control(ID_MAP_CODE), EM_SETLIMITTEXT, 19, 0);
        AddControl("BUTTON", "Buscar mapas", WS_TABSTOP, ID_MAP_SEARCH, 505, 377, 190, 32, 6);
        AddControl("LISTBOX", "", LBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_TABSTOP, ID_HISTORY, 30, 65, 680, 420, 7);
        AddControl("STATIC", "Nome do evento", 0, 0, 30, 451, 120, 20, 5);
        AddControl("EDIT", "Arena", ES_AUTOHSCROLL | WS_TABSTOP, ID_EVENT_NAME, 30, 474, 155, 23, 5);
        SendMessage(Control(ID_EVENT_NAME), EM_SETLIMITTEXT, 30, 0);
        AddControl("BUTTON", "Salvar evento", WS_TABSTOP, ID_EVENT_SAVE, 199, 470, 140, 30, 5);
        AddControl("BUTTON", "Iniciar salvo", WS_TABSTOP, ID_EVENT_RUN, 350, 470, 140, 30, 5);
        AddControl("BUTTON", "Encerrar evento", WS_TABSTOP, ID_EVENT_END, 501, 470, 140, 30, 5);
        AddControl("EDIT", "", ES_AUTOHSCROLL | WS_TABSTOP, ID_ADV_TARGET, 30, 68, 200, 25, 8);
        SendMessage(Control(ID_ADV_TARGET), EM_SETLIMITTEXT, 19, 0);
        AddControl("BUTTON", "Inspecionar personagem", WS_TABSTOP, ID_INSPECT, 245, 65, 205, 32, 8);
        AddControl("BUTTON", "Mapas e jogadores", WS_TABSTOP, ID_SESSION_ALL, 465, 65, 225, 32, 8);
        AddControl("BUTTON", "Restaurar HP / MP", WS_TABSTOP, ID_RECOVER_HP, 30, 112, 150, 30, 8);
        AddControl("BUTTON", "Limpar maldicoes", WS_TABSTOP, ID_RECOVER_CURSE, 190, 112, 160, 30, 8);
        AddControl("BUTTON", "Ressuscitar", WS_TABSTOP, ID_REVIVE, 360, 112, 130, 30, 8);
        AddControl("BUTTON", "Historico do servidor", WS_TABSTOP, ID_AUDIT, 500, 112, 190, 30, 8);
        AddControl("BUTTON", "Criar alvo (aba Monstros)", WS_TABSTOP, ID_TRAINING, 30, 156, 225, 30, 8);
        AddControl("BUTTON", "Medir dano dos alvos", WS_TABSTOP, ID_TRAINING_STATS, 268, 156, 205, 30, 8);
        AddControl("BUTTON", "Avisar todos os mapas", WS_TABSTOP, ID_ANNOUNCE_ALL, 485, 156, 205, 30, 8);
        AddControl("EDIT", "", ES_MULTILINE | ES_READONLY | WS_VSCROLL | WS_TABSTOP, ID_ADV_DETAILS, 30, 201, 660, 288, 8);
        AddControl("BUTTON", "Atualizar dados", WS_TABSTOP, ID_REFRESH, 30, 510, 160, 30);
        AddControl("STATIC", "Conectando ao servidor...", 0, ID_STATUS, 210, 508, 500, 46);
        FillItems();
        FillEffects();
        ShowPage(0);
        SetBusy(false);
    }

    bool EventSection(char (&section)[48])
    {
        char name[31];
        GetWindowTextA(Control(ID_EVENT_NAME), name, sizeof(name));
        if (!name[0] || strspn(name, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 _-") != strlen(name))
        {
            Status("Nome do evento: letras, numeros, espacos, hifen e sublinhado.");
            return false;
        }
        sprintf_s(section, "Evento %s", name);
        return true;
    }

    bool SaveEvent()
    {
        char section[48];
        if (!EventSection(section)) return false;
        DWORD quantity, waves, seconds, x, y;
        if (SendMessage(Control(ID_MONSTER), CB_GETCURSEL, 0, 0) == CB_ERR ||
            !ReadNumber(ID_MONSTER_COUNT, 20, quantity) || !quantity ||
            !ReadNumber(ID_WAVE_COUNT, 5, waves) || !waves ||
            !ReadNumber(ID_WAVE_DELAY, 60, seconds) || seconds < 3)
        {
            Status("Escolha monstro, quantidade, ondas e intervalo validos.");
            return false;
        }
        const LocalAdminState& point = returnPoint.map[0] ? returnPoint : currentState;
        x = point.mapX; y = point.mapY;
        if (!point.map[0] || !x || !y) { Status("Marque o ponto do evento na aba Mapa / Sessao."); return false; }
        char setup[180];
        const int monster = SelectedValue(ID_MONSTER);
        sprintf_s(setup, "%s %lu %lu %d %lu %lu %lu", point.map, x, y, monster, quantity, waves, seconds);
        char announcement[120];
        GetWindowTextA(Control(ID_ANNOUNCEMENT), announcement, sizeof(announcement));
        if (!WritePrivateProfileStringA(section, "Anuncio", announcement, eventFile) ||
            !WritePrivateProfileStringA(section, "Arena", setup, eventFile))
        {
            Status("Falha ao gravar gm-events.ini.");
            return false;
        }
        return true;
    }

    void OnCommand(int id, int notification)
    {
        if (id == ID_MONSTER_SEARCH && notification == EN_CHANGE) { FillMonsters(); return; }
        if (id == ID_PROGRESS_KIND && notification == CBN_SELCHANGE) { FillProgressList(); return; }
        if (id == ID_PROGRESS_INDEX && notification == CBN_SELCHANGE) { FillProgressValue(); return; }
        if (id == ID_SEARCH && notification == EN_CHANGE) { FillItems(); return; }
        if (id == ID_LEGEND_VARIANT && notification == EN_CHANGE) { InvalidatePreview(); return; }
        if (id == ID_LEGEND && notification == BN_CLICKED) { InvalidatePreview(); SetBusy(false); return; }
        if (id == ID_DYNAMIC && notification == BN_CLICKED) { InvalidatePreview(); FillEffects(); return; }
        if ((id >= ID_ITEMS && id <= ID_HIGH) && (notification == EN_CHANGE || notification == CBN_SELCHANGE || notification == BN_CLICKED))
            InvalidatePreview();
        if (id == ID_WAVE_STOP && notification == BN_CLICKED)
        {
            KillTimer(panel, 2);
            wavesRemaining = 0;
            SetBusy(pendingRequest != 0);
            Status("Ondas canceladas. Os grupos ativos podem ser removidos pelo ID.");
            return;
        }
        if (notification != BN_CLICKED || pendingRequest) return;
        LocalAdminRequest request = {};
        request.state = currentState;
        DWORD value = 0;
        switch (id)
        {
        case ID_EVENT_SAVE:
            if (SaveEvent()) Status("Evento salvo. Use o mesmo nome para iniciar novamente.");
            return;
        case ID_EVENT_RUN:
            {
                if (wavesRemaining || activeEventGroup) { Status("Encerre o evento ou pare as ondas atuais."); return; }
                char section[48], setup[180], map[20];
                if (!EventSection(section)) return;
                unsigned long x, y, quantity, waves, seconds;
                int monster;
                GetPrivateProfileStringA(section, "Arena", "", setup, sizeof(setup), eventFile);
                if (sscanf_s(setup, "%19s %lu %lu %d %lu %lu %lu", map, unsigned(sizeof(map)),
                    &x, &y, &monster, &quantity, &waves, &seconds) != 7 ||
                    x < 1 || x > 32767 || y < 1 || y > 32767 || !quantity || quantity > 20 ||
                    !waves || waves > 5 || seconds < 3 || seconds > 60)
                { Status("Evento ausente ou com configuracao invalida."); return; }
                if (_stricmp(currentState.map, map)) { Status("Entre no mapa do evento e atualize os dados."); return; }
                request.operation = LA_SUMMON;
                request.progressionIndex = 3;
                request.itemNumber = monster;
                request.quantity = quantity;
                request.state.mapX = x;
                request.state.mapY = y;
                GetPrivateProfileStringA(section, "Anuncio", "", request.announcement, sizeof(request.announcement), eventFile);
                wavesRemaining = waves - 1;
                waveDelay = seconds * 1000;
                waveRequest = request;
                activeEventLocation = request.state;
            }
            break;
        case ID_EVENT_END:
            if (_stricmp(currentState.map, activeEventLocation.map)) { Status("Volte ao mapa do evento."); return; }
            KillTimer(panel, 2);
            wavesRemaining = 0;
            request.operation = LA_SUMMON_REMOVE;
            request.progressionIndex = 3;
            request.previewToken = activeEventGroup;
            request.state = activeEventLocation;
            break;
        case ID_INSPECT:
            request.operation = LA_INSPECT;
            GetWindowTextA(Control(ID_ADV_TARGET), request.target, sizeof(request.target));
            break;
        case ID_RECOVER_HP:
        case ID_RECOVER_CURSE:
        case ID_REVIVE:
            request.operation = id == ID_REVIVE ? LA_REVIVE : LA_RECOVER;
            request.progressionIndex = id - ID_RECOVER_HP;
            break;
        case ID_TRAINING:
            if (SendMessage(Control(ID_MONSTER), CB_GETCURSEL, 0, 0) == CB_ERR)
            { Status("Selecione o modelo do alvo na aba Monstros."); return; }
            request.operation = LA_TRAINING;
            request.itemNumber = SelectedValue(ID_MONSTER);
            request.quantity = 1;
            break;
        case ID_TRAINING_STATS: request.operation = LA_TRAINING_STATS; break;
        case ID_SESSION_ALL: request.operation = LA_SESSION_ALL; break;
        case ID_ANNOUNCE_ALL:
            request.operation = LA_ANNOUNCE_ALL;
            GetWindowTextA(Control(ID_ANNOUNCEMENT), request.announcement, sizeof(request.announcement));
            if (!request.announcement[0]) { Status("Digite o aviso na aba Mapa / Sessao."); return; }
            break;
        case ID_AUDIT: request.operation = LA_AUDIT; break;
        case ID_LEGEND_CATALOG:
            request.operation = LA_LEGEND_CATALOG;
            request.itemNumber = SelectedValue(ID_ITEMS);
            if (!request.itemNumber) { Status("Selecione um item primeiro."); return; }
            break;
        case ID_MAP_SEARCH:
            request.operation = LA_MAP_SEARCH;
            GetWindowTextA(Control(ID_MAP_CODE), request.state.map, sizeof(request.state.map));
            break;
        case ID_UNDO_TACTIC: request.operation = LA_UNDO_TACTIC; break;
        case ID_SAVE_PRESET:
            request.operation = LA_SESSION;
            request.progressionIndex = 2;
            break;
        case ID_APPLY_PRESET:
            if (!preset.character[0]) return;
            {
                char confirmation[220];
                sprintf_s(confirmation, "Aplicar o preset de %s (nivel %d)? Substitui atributos, nivel, XP, pontos livres, as 13 taticas e magias Wizard/Priest.",
                    preset.character, preset.level);
                if (MessageBoxA(panel, confirmation, "Reaplicar preset", MB_OKCANCEL) != IDOK) return;
            }
            request.operation = LA_PRESET;
            request.state = preset;
            break;
        case ID_SUMMON_LIST: request.operation = LA_SUMMON_LIST; break;
        case ID_SUMMON_REMOVE:
            if (!ReadNumber(ID_SUMMON_GROUP, 0xffffffffUL, value) || !value) { Status("Informe o ID do grupo."); return; }
            request.operation = LA_SUMMON_REMOVE;
            request.previewToken = value;
            break;
        case ID_MARK:
            request.operation = LA_SESSION;
            request.progressionIndex = 1;
            break;
        case ID_RETURN:
            request.operation = LA_TELEPORT;
            if (_stricmp(returnPoint.map, currentState.map) != 0)
            {
                Status("Volte ao mapa do ponto marcado antes de restaurar suas coordenadas.");
                return;
            }
            request.state = returnPoint;
            break;
        case ID_TELEPORT:
            request.operation = LA_TELEPORT;
            if (GetWindowTextLengthA(Control(ID_MAP_CODE)))
                GetWindowTextA(Control(ID_MAP_CODE), request.state.map, sizeof(request.state.map));
            if (!ReadNumber(ID_MAP_X, 32767, value)) { Status("Coordenada X invalida."); return; }
            request.state.mapX = value;
            if (!ReadNumber(ID_MAP_Y, 32767, value)) { Status("Coordenada Y invalida."); return; }
            request.state.mapY = value;
            break;
        case ID_SESSION: request.operation = LA_SESSION; break;
        case ID_GATHER:
            if (MessageBoxA(panel, "Reunir os outros jogadores deste mapa na sua posicao?", "Painel GM", MB_OKCANCEL) != IDOK) return;
            request.operation = LA_GATHER;
            break;
        case ID_ANNOUNCE:
            request.operation = LA_ANNOUNCE;
            GetWindowTextA(Control(ID_ANNOUNCEMENT), request.announcement, sizeof(request.announcement));
            break;
        case ID_SUMMON:
        {
            if (!catalogReady || SendMessage(Control(ID_MONSTER), CB_GETCURSEL, 0, 0) == CB_ERR) { Status("Selecione um monstro do catalogo do servidor."); return; }
            request.operation = LA_SUMMON; request.itemNumber = SelectedValue(ID_MONSTER);
            if (!ReadNumber(ID_MONSTER_COUNT, 20, value) || !value) { Status("Quantidade: 1-20."); return; }
            request.quantity = value;
            if (wavesRemaining) { Status("Pare as ondas atuais antes de iniciar outras."); return; }
            if (!ReadNumber(ID_WAVE_COUNT, 5, value) || !value) { Status("Ondas: 1 a 5."); return; }
            const DWORD count = value;
            if (!ReadNumber(ID_WAVE_DELAY, 60, value) || value < 3) { Status("Intervalo: 3 a 60 segundos."); return; }
            wavesRemaining = count - 1;
            waveDelay = value * 1000;
            waveRequest = request;
            waveRequest.progressionIndex = 1;
            break;
        }
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
        case ID_CREATE:
            request.operation = id == ID_CREATE ? LA_CREATE : LA_PREVIEW;
            request.previewToken = id == ID_CREATE ? previewToken : 0;
            request.itemNumber = SelectedValue(ID_ITEMS);
            if (!request.itemNumber || !ReadNumber(ID_QUANTITY, 1000000, value) || !value) { Status("Selecione um item e uma quantidade valida."); return; }
            request.quantity = value;
            if (!ReadNumber(ID_DURABILITY, 65535, value)) { Status("Durabilidade: 0 a 65535."); return; }
            request.durability = value;
            if (IsDlgButtonChecked(panel, ID_LEGEND) == BST_CHECKED)
            {
                request.highItem = 2;
                if (!ReadNumber(ID_LEGEND_VARIANT, 9, value)) { Status("Variante: 0 a 9."); return; }
                request.legendVariant = value;
                if (value)
                {
                    if (!ReadNumber(ID_GRADE, 8, value) || value < 3) { Status("Grau Legend: 3 a 8."); return; }
                    request.grade = value;
                }
                break;
            }
            if (!ReadNumber(ID_GRADE, 15, value)) { Status("Grau: 0 a 15."); return; }
            request.grade = value;
            if (request.grade && !catalogReady) { Status("Aguarde o catalogo de efeitos do servidor."); return; }
            for (int i = 0; i < 3; ++i) request.effects[i] = SelectedValue(ID_EFFECT1 + i);
            request.dynamicEffect = IsDlgButtonChecked(panel, ID_DYNAMIC) == BST_CHECKED;
            request.highItem = IsDlgButtonChecked(panel, ID_HIGH) == BST_CHECKED;
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
            if (wParam == 2)
            {
                if (pendingRequest) return 0;
                KillTimer(window, 2);
                if (!IsWindowVisible(panel) || !Hero || !SMenu[MN_MAININTERFACE].bActive || wavesRemaining <= 0)
                {
                    wavesRemaining = 0;
                    SetBusy(false);
                    return 0;
                }
                --wavesRemaining;
                SendRequest(waveRequest);
                return 0;
            }
            KillTimer(window, 2);
            wavesRemaining = 0;
            if (wParam != 1) break;
            KillTimer(window, 1);
            if (pendingOperation == LA_REVIVE)
                AddCurrentStatusMessage(255, 100, 100, "Sem resposta ao reviver. O MapServer precisa da atualizacao do painel GM.");
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

static void EnsureLocalAdminPanel()
{
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
}

void ToggleLocalAdminPanel()
{
    if (!GetPrivateProfileIntA("local", "AdminPanel", 0, ".\\dragon.ini")) return;
    if (!Hero || !SMenu[MN_MAININTERFACE].bActive) return;
    EnsureLocalAdminPanel();
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
    if (!panel || reply.version != LOCAL_ADMIN_VERSION) return;
    if (reply.operation == LA_SESSION_ALL && reply.previewToken == 1)
    {
        if (reply.requestId != rosterRequest) return;
        reply.details[sizeof(reply.details) - 1] = 0;
        HWND details = Control(ID_ADV_DETAILS);
        const int end = GetWindowTextLengthA(details);
        if (end + strlen(reply.details) >= 32000) return;
        std::vector<char> existing(end + 1);
        GetWindowTextA(details, existing.data(), static_cast<int>(existing.size()));
        SetWindowTextA(details, (std::string(existing.data()) + reply.details).c_str());
        return;
    }
    if (reply.requestId != pendingRequest) return;
    reply.message[sizeof(reply.message) - 1] = 0;
    reply.details[sizeof(reply.details) - 1] = 0;
    if (reply.details[0])
    {
        SetWindowTextA(Control(ID_ADV_DETAILS), reply.details);
        TabCtrl_SetCurSel(tabControl, 8);
        ShowPage(8);
    }
    if (eventReply && reply.operation == LA_SUMMON && reply.success)
    {
        activeEventGroup = reply.summonGroup;
        waveRequest.summonGroup = reply.summonGroup;
    }
    if (eventReply && reply.operation == LA_SUMMON_REMOVE && reply.success)
        activeEventGroup = 0;
    reply.state.character[sizeof(reply.state.character) - 1] = 0;
    KillTimer(panel, 1);
    pendingRequest = 0;
    canUndo = reply.canUndo != 0;
    if (reply.success)
    {
        const LocalAdminState previous = currentState;
        currentState = reply.state;
        if (previous.character[0] && strcmp(previous.character, currentState.character))
        {
            returnPoint = {};
            preset = {};
            SendMessage(Control(ID_HISTORY), LB_RESETCONTENT, 0, 0);
        }
        if (capturePreset)
        {
            preset = currentState;
            strcpy_s(reply.message, "Preset capturado nesta sessao.");
        }
        if (markOnReply)
        {
            returnPoint = currentState;
            strcpy_s(reply.message, "Posicao marcada nesta sessao.");
        }
        char mapInfo[100];
        sprintf_s(mapInfo, "%s | X %d | Y %d", currentState.map, currentState.mapX, currentState.mapY);
        SetWindowTextA(Control(ID_MAP_INFO), mapInfo);
        if (reply.operation == LA_READ || reply.operation == LA_TELEPORT || markOnReply)
        {
            SetNumber(ID_MAP_X, currentState.mapX);
            SetNumber(ID_MAP_Y, currentState.mapY);
        }
        if (reply.operation == LA_PROGRESSION || reply.operation == LA_UNDO_TACTIC || reply.operation == LA_PRESET)
        {
            for (int i = 0; i < 13; ++i)
            {
                if (previous.skills[88 + i] == currentState.skills[88 + i] &&
                    previous.tacticExp[i] == currentState.tacticExp[i] &&
                    previous.tacticStep[i] == currentState.tacticStep[i]) continue;
                char detail[160];
                sprintf_s(detail, "Tatica %d: nivel %d -> %d | XP %lu -> %lu", 88 + i,
                    int(previous.skills[88 + i]), int(currentState.skills[88 + i]),
                    static_cast<unsigned long>(previous.tacticExp[i]), static_cast<unsigned long>(currentState.tacticExp[i]));
                SendMessageA(Control(ID_HISTORY), LB_INSERTSTRING, 0, reinterpret_cast<LPARAM>(detail));
            }
        }
        if (Hero) {
            Hero->aStepInfo[CLS_STEP] = static_cast<char>(currentState.dualStep);
            Hero->aStepInfo[DUAL_CLS] = static_cast<char>(currentState.dualClass);
            Hero->aStepInfo[CSP_STEP] = static_cast<char>(currentState.combatPoints);
        }
        memcpy(SCharacterData.SkillId, currentState.skills, sizeof(currentState.skills));
        memcpy(SCharacterData.MagicId, SCharacterData.nCharacterData[SPELL] == PRIEST_SPELL ? currentState.priest : currentState.wizard, 200);
        SCharacterData.faith = currentState.extraProgress[5];
        if (reply.operation == LA_PROGRESSION || reply.operation == LA_PRESET) {
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
        if (reply.operation == LA_READ || reply.operation == LA_ATTRIBUTES || reply.operation == LA_UNDO_ATTRIBUTES || reply.operation == LA_PRESET)
        {
            const int kinds[] = { STR, DEX, CON, WIS, INT_, CHA, MOVP, ENDU, MOR, LUCK, WSPS };
            for (int i = 0; i < LOCAL_ADMIN_ATTRIBUTE_COUNT; ++i)
                SCharacterData.nCharacterAbility[kinds[i]] = currentState.attributes[i];
            SCharacterData.nLevel = static_cast<short>(currentState.level);
            SCharacterData.nExp = currentState.experience;
            SCharacterData.LvUpPoint = currentState.points;
        }
        FillState(reply.operation == LA_READ || reply.operation == LA_ATTRIBUTES || reply.operation == LA_UNDO_ATTRIBUTES || reply.operation == LA_PRESET);
        if (reply.operation == LA_PREVIEW)
        {
            previewToken = reply.previewToken;
            SetWindowTextA(Control(ID_PREVIEW_TEXT), reply.message);
        }
        if (reply.operation == LA_CREATE) previewToken = 0;
    }
    if (reply.operation != LA_READ && reply.operation != LA_PREVIEW)
    {
        SendMessageA(Control(ID_HISTORY), LB_INSERTSTRING, 0, reinterpret_cast<LPARAM>(reply.message));
        while (SendMessage(Control(ID_HISTORY), LB_GETCOUNT, 0, 0) > 100)
            SendMessage(Control(ID_HISTORY), LB_DELETESTRING, 100, 0);
    }
    if (reply.operation == LA_SUMMON && wavesRemaining > 0)
    {
        if (reply.success && IsWindowVisible(panel))
        {
            if (waveRequest.progressionIndex != 3) waveRequest.state = reply.state;
            SetTimer(panel, 2, waveDelay, NULL);
        }
        else wavesRemaining = 0;
    }
    Status(reply.message);
    if (reply.operation == LA_REVIVE)
        AddCurrentStatusMessage(reply.success ? 80 : 255, reply.success ? 220 : 100, 100, "%s", reply.message);
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


namespace
{
    LRESULT CALLBACK LifeButtonProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_COMMAND && LOWORD(wParam) == ID_REVIVE && HIWORD(wParam) == BN_CLICKED)
        {
            if (!Hero || Hero->viewtype != VIEWTYPE_GHOST_ || pendingRequest) return 0;
            // Replies and timeout work even when Ctrl+F12 has never been opened.
            EnsureLocalAdminPanel();
            LocalAdminRequest request = {};
            request.operation = LA_REVIVE;
            SendRequest(request);
            SetForegroundWindow(g_hwndMain);
            return 0;
        }
        if (message == WM_CLOSE) { ShowWindow(window, SW_HIDE); return 0; }
        return DefWindowProcA(window, message, wParam, lParam);
    }
}

void UpdateLocalAdminLifeButton()
{
    static const bool enabled = GetPrivateProfileIntA("local", "AdminPanel", 0, ".\\dragon.ini") != 0;
    const bool ghost = enabled && Hero && SMenu[MN_MAININTERFACE].bActive && Hero->viewtype == VIEWTYPE_GHOST_;
    if (!ghost || IsIconic(g_hwndMain))
    {
        if (lifeWindow) ShowWindow(lifeWindow, SW_HIDE);
        return;
    }
    if (!lifeWindow)
    {
        WNDCLASSA cls = {};
        cls.lpfnWndProc = LifeButtonProcedure;
        cls.hInstance = g_hInstance;
        cls.hCursor = LoadCursor(NULL, IDC_ARROW);
        cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        cls.lpszClassName = "DragonRajaFamilyRevive";
        RegisterClassA(&cls);
        lifeWindow = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls.lpszClassName, "",
            WS_POPUP | WS_BORDER, 0, 0, 250, 48, g_hwndMain, NULL, g_hInstance, NULL);
        lifeButton = CreateWindowExA(0, "BUTTON", "Reviver instantaneamente",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 4, 4, 240, 38, lifeWindow,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_REVIVE)), g_hInstance, NULL);
        SendMessage(lifeButton, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    }
    if (!lifeWindow) return;
    RECT area;
    GetClientRect(g_hwndMain, &area);
    POINT position = { (area.right - 250) / 2, area.bottom - 110 };
    ClientToScreen(g_hwndMain, &position);
    SetWindowPos(lifeWindow, NULL, position.x, position.y, 250, 48, SWP_NOACTIVATE | SWP_NOZORDER);
    EnableWindow(lifeButton, pendingRequest == 0);
    SetWindowTextA(lifeButton, pendingRequest && pendingOperation == LA_REVIVE ? "Revivendo..." : "Reviver instantaneamente");
    ShowWindow(lifeWindow, SW_SHOWNOACTIVATE);
}
