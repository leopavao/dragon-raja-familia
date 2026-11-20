#include "../stdafx.h"
#include "DefaultHeader.h"
#include "MenuServer.h"
#include "CItem.h"
#include "Kein_item.h"
#include "SkillMgr.h"
#include "skill_lsw.h"
#include "MySql.h"
#include "RareEffectMgr.h"
#include "../LowerLayers/mylog.h"
#include "../LowerLayers/servertable.h"
#include "../../Shared/LocalAdminProtocol.h"
#include "LocalAdminServer.h"
#include "SymbolItemMgr.h"
#include "DualManager.h"
#include "Scrp_exe.h"
#include "Npc_pattern.h"
#include "Npclist.h"
#include "Op_magic.h"
#include "Map.h"
#include "MapInfo.h"
#include <map>
#include <deque>
#include <stdarg.h>
#include <string>

extern void QuestTeleport(int connection, int x, int y);
extern void SendBBS2(char* text, short connection);
extern void ResetAbility(CHARLIST* character);
extern int AddCRC(void* source, int size, int step);
extern void SendLoginServerForCharUpdate(short cn);
static_assert(sizeof(LocalAdminCatalog) <= sizeof(((t_packet*)0)->u), "Catalog packet exceeds game payload");
static_assert(sizeof(LocalAdminReply) <= sizeof(((t_packet*)0)->u), "State packet exceeds game payload");

namespace
{
    struct ItemDraft
    {
        std::string character;
        DWORD token;
        ItemAttr item;
        int copies;
    };

    struct AttributeUndo
    {
        std::string character;
        LocalAdminState state;
    };

    std::map<int, ItemDraft> drafts;
    std::map<int, AttributeUndo> undoAttributes;
    DWORD nextPreviewToken = 0;
    std::map<int, DWORD> summonTimes;
    DWORD nextSummonGroup = 0;
    std::map<DWORD, DWORD> eventGroups;
    struct RosterRequest
    {
        DWORD ticket, playerId, requestId, started;
    };
    std::map<int, RosterRequest> rosterRequests;
    DWORD nextRosterTicket = 0;
    const char* auditPath = "./gm-audit.log";

    void AppendDetails(LocalAdminReply& reply, const char* format, ...)
    {
        const size_t used = strlen(reply.details);
        if (used >= sizeof(reply.details) - 1) return;
        va_list args;
        va_start(args, format);
        _vsnprintf_s(reply.details + used, sizeof(reply.details) - used, _TRUNCATE, format, args);
        va_end(args);
    }

    void AppendMapRoster(LocalAdminReply& reply)
    {
        AppendDetails(reply, "%s:", MapName);
        int players = 0;
        for (int i = DRAGON_CONNECTIONS_START; i < DRAGON_MAX_CONNECTIONS; ++i)
        {
            if (connections[i].state != CONNECT_JOIN || !CheckServerId(i)) continue;
            if (players < 40) AppendDetails(reply, " %.19s", connections[i].name);
            ++players;
        }
        if (players > 40) AppendDetails(reply, " ...");
        AppendDetails(reply, " (%d)\r\n", players);
    }

    void SendRoster(WORD port, const LocalAdminMapRoster& roster)
    {
        t_packet packet = {};
        packet.h.header.type = CMD_LOCAL_ADMIN_MAP_ROSTER;
        packet.h.header.size = sizeof(roster);
        memcpy(&packet.u, &roster, sizeof(roster));
        g_pServerTable->SendRajaPacketToOtherMapServer(port, reinterpret_cast<char*>(&packet),
            packet.h.header.size + sizeof(t_header));
    }

    void Audit(int connection, const LocalAdminRequest& request, const LocalAdminReply& reply)
    {
        FILE* file = fopen(auditPath, "a");
        if (!file) return;
        SYSTEMTIME now;
        GetLocalTime(&now);
        fprintf(file, "%04u-%02u-%02u %02u:%02u:%02u | %.19s | %.19s | operacao %u | alvo %.19s | item %d | quantidade %d | sucesso %u\n",
            now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
            connections[connection].id, reply.state.character, request.operation, request.target,
            request.itemNumber, request.quantity, reply.success);
        fclose(file);
    }
    struct TacticSave
    {
        DWORD playerId;
        LocalAdminReply reply;
    };
    std::map<int, TacticSave> tacticSaves;
    struct TacticUndo
    {
        std::string character;
        DWORD playerId;
        int index, oldStep, newStep;
        DWORD oldExperience, newExperience;
    };
    std::map<int, TacticUndo> tacticUndo;

    void SendAdminReply(int connection, const LocalAdminReply& reply)
    {
        t_packet packet = {};
        packet.h.header.type = CMD_LOCAL_ADMIN;
        packet.h.header.size = sizeof(reply);
        memcpy(&packet.u, &reply, sizeof(reply));
        QueuePacket(connections, connection, &packet, 1);
    }

    int MaximizeSummonDropRarity(CHARLIST* npc)
    {
        int upgraded = 0;
        ItemAttr* inventory = &npc->inv[0][0][0];
        for (int i = 0; i < 72; ++i) {
            ItemAttr& item = inventory[i];
            if (!item.item_no) continue;
            CItem* definition = ItemUnit(item);
            if (!definition) continue;
            const int kind = definition->GetItemKind();
            if (kind < 0 || kind >= MAX_ITEM_MUTANT_KIND || !ItemMutantKind[kind].upgrade_type) continue;
            RareMain existing = {};
            memcpy(&existing, &item.attr[IATTR_RARE_MAIN], sizeof(existing));
            if (existing.IsDynamicRare || existing.iHighLevel == H_LV_LEGEND_ITEM ||
                (existing.grade && existing.soksung1)) continue;
            RareMain generated = {};
            const int grade = existing.grade ? existing.grade : 1;
            const int high = existing.iHighLevel == H_LV_HIGH_ITEM ? H_LV_HIGH_ITEM : H_LV_NO_HIGH_ITEM;
            ItemAttr rareItem = ItemMgr.GiveRareItem(item.item_no, 0, RARE_ABLE, 1, grade, grade, generated, high);
            if (rareItem.item_no == item.item_no && generated.grade && generated.soksung1) {
                // Keep the species-selected item, quantity, durability, lifetime and all other attributes.
                item.attr[IATTR_RARE_MAIN] = rareItem.attr[IATTR_RARE_MAIN];
                ++upgraded;
            }
        }
        return upgraded;
    }

    void SendEffectCatalog(int cn, DWORD requestId)
    {
        LocalAdminCatalog catalog = {};
        catalog.version = LOCAL_ADMIN_VERSION;
        catalog.requestId = requestId;
        catalog.first = 1;
        for (int dynamic = 0; dynamic < 2; ++dynamic)
        {
            const int count = dynamic ? MAX_ITEM_FUNCTION : MAX_ITEM_RARE;
            for (int id = 1; id < count && id <= 255; ++id)
            {
                const char* name = dynamic ? ItemFunction[id].Name : ItemRare[id].LocalName;
                if (!name || !name[0]) continue;
                LocalAdminEffect& entry = catalog.effects[catalog.count++];
                entry.id = id;
                entry.dynamicEffect = dynamic;
                strncpy_s(entry.name, name, _TRUNCATE);
                if (catalog.count == 24)
                {
                    t_packet packet = {};
                    packet.h.header.type = CMD_LOCAL_ADMIN_CATALOG;
                    packet.h.header.size = sizeof(catalog);
                    memcpy(&packet.u, &catalog, sizeof(catalog));
                    QueuePacket(connections, cn, &packet, 1);
                    catalog.count = 0;
                    catalog.first = 0;
                }
            }
        }
        for (int id = 0; id < MAX_SKILLMAIN * 2; ++id)
        {
            const int source = id % MAX_SKILLMAIN;
            if (!SkillTbl[source].Skill_Minute[0]) continue;
            if (id >= MAX_SKILLMAIN && (SkillTbl[source].inclusive < 0 || SkillTbl[source].inclusive >= 45)) continue;
            LocalAdminEffect& entry = catalog.effects[catalog.count++];
            entry.id = id < MAX_SKILLMAIN ? source : SkillTbl[source].inclusive;
            entry.dynamicEffect = id < MAX_SKILLMAIN ? 2 : 3;
            strncpy_s(entry.name, SkillTbl[source].Skill_Minute, _TRUNCATE);
            if (catalog.count == 24) {
                t_packet chunk = {};
                chunk.h.header.type = CMD_LOCAL_ADMIN_CATALOG;
                chunk.h.header.size = sizeof(catalog);
                memcpy(&chunk.u, &catalog, sizeof(catalog));
                QueuePacket(connections, cn, &chunk, 1);
                catalog.count = 0; catalog.first = 0;
            }
        }
        for (int id = 0; id < Num_Of_NPC_Generation; ++id) {
            if (!NPC_Gen_Ref[id].Name[0] || NPC_Gen_Ref[id].nHpMax <= 0 || NPC_Gen_Ref[id].SprNO <= 0) continue;
            LocalAdminEffect& entry = catalog.effects[catalog.count++];
            entry.id = id; entry.dynamicEffect = 4;
            strncpy_s(entry.name, NPC_Gen_Ref[id].Name, _TRUNCATE);
            if (catalog.count == 24) {
                t_packet chunk = {};
                chunk.h.header.type = CMD_LOCAL_ADMIN_CATALOG;
                chunk.h.header.size = sizeof(catalog);
                memcpy(&chunk.u, &catalog, sizeof(catalog));
                QueuePacket(connections, cn, &chunk, 1);
                catalog.count = 0; catalog.first = 0;
            }
        }
        catalog.last = 1;
        t_packet packet = {};
        packet.h.header.type = CMD_LOCAL_ADMIN_CATALOG;
        packet.h.header.size = sizeof(catalog);
        memcpy(&packet.u, &catalog, sizeof(catalog));
        QueuePacket(connections, cn, &packet, 1);
    }

    void ReadState(CHARLIST* ch, LocalAdminState& state)
    {
        memset(&state, 0, sizeof(state));
        strcpy_s(state.character, ch->Name);
        const short values[] = { ch->Str, ch->Dex, ch->Con, ch->Wis, ch->Int,
            ch->Char, ch->MoveP, ch->Endu, ch->Moral, ch->Luck, ch->wsps };
        for (int i = 0; i < LOCAL_ADMIN_ATTRIBUTE_COUNT; ++i)
            state.attributes[i] = values[i];
        state.level = ch->GetLevel();
        state.experience = ch->Exp;
        state.points = ch->GetReservedPoint();
        state.gold = GetMoneyByItem(ch);
        state.bank = ch->GetBankMoney();
        state.emblems = GetItemMuchByMuchItem(ch, 10057);
        state.baseClass = ch->Class;
        state.dualClass = ch->GetDualClass();
        state.dualStep = ch->GetClassStep();
        state.symbolGrade = ch->quick[5].attr[0];
        state.symbolNumber = ch->quick[5].item_no;
        state.fame = ch->fame;
        state.ladder = ch->LadderScore;
        state.combatPoints = ch->GetCombatPoint();
        const int scriptIndices[] = { s_GhostLvDef, s_GhostEndCountDef, DUAL_FAME_FIELD, HONOR_QUEST_NO, HONOR_MAX_QUEST_NO };
        for (int i = 0; i < 5; ++i) state.extraProgress[i] = var[ch->GetServerID()][scriptIndices[i]];
        state.extraProgress[5] = ch->faith;
        state.extraProgress[6] = ch->god_cast_level;
        memcpy(state.skills, ch->Skill, sizeof(state.skills));
        memcpy(state.wizard, ch->Ws, sizeof(state.wizard));
        memcpy(state.priest, ch->Ps, sizeof(state.priest));
        memcpy(state.tacticExp, ch->tac_skillEXP, sizeof(state.tacticExp));
        for (int i = 0; i < NO_TAC; ++i) state.tacticStep[i] = ch->GetTacticsStep(i);
        strcpy_s(state.map, MapName);
        state.mapX = ch->X / TILE_SIZE;
        state.mapY = ch->Y / TILE_SIZE;
        for (int i = 0; i < 45; ++i) {
            state.workExp[i] = ch->skillexp[i].skillexp;
            state.workRare[i] = ch->skillexp[i].rare;
        }
    }

    bool ValidAttributes(const LocalAdminState& state)
    {
        for (int i = 0; i < LOCAL_ADMIN_ATTRIBUTE_COUNT; ++i)
            if (state.attributes[i] < 0 || state.attributes[i] > 32767)
                return false;
        return state.level >= 1 && state.level <= MAX_EXP_LEVEL &&
            state.points >= 0 && state.points <= 32767;
    }

    bool GetTacticStartingExperience(int level, DWORD& experience)
    {
        if (level < 1 || level > MAX_TAC_LEVEL || level >= Num_Of_NPC_Lev) return false;
        const NPCLev_to_Exp& range = NPC_Lev_Ref[level];
        int minimum = range.nMinExp;
        if (minimum < 0 || range.nMaxExp <= 0) return false;
        // Login and the client select the first matching upper bound in this step.
        for (int previous = 1; previous < level; ++previous)
        {
            const NPCLev_to_Exp& earlier = NPC_Lev_Ref[previous];
            if (earlier.nStep != range.nStep || minimum > earlier.nMaxExp) continue;
            if (earlier.nMaxExp >= range.nMaxExp) return false;
            minimum = earlier.nMaxExp + 1;
        }
        if (minimum > range.nMaxExp) return false;
        experience = minimum;
        return true;
    }

    void SetTactic(CHARLIST* ch, int index, int level, DWORD experience)
    {
        ch->SetTacticsStep(index, NPC_Lev_Ref[level].nStep);
        ch->tac_skillEXP[index] = experience;
        ch->ConvertTacticsLevel(index);
        ch->SendCharInfoTactic(index);
    }

    bool ValidCombatTotal(CHARLIST* ch, int points, int changed = -1, int newLevel = 0)
    {
        int total = points;
        for (int i = 0; i < 36; ++i) total += i == changed ? newLevel : ch->GetCombatLevel(LIGHTNING_BOOM + i);
        return total <= NPC_Lev_Ref[ch->GetLevel()].nCspMax;
    }

    void ApplyAttributes(CHARLIST* ch, const LocalAdminState& state)
    {
        short* fields[] = { &ch->Str, &ch->Dex, &ch->Con, &ch->Wis, &ch->Int,
            &ch->Char, &ch->MoveP, &ch->Endu, &ch->Moral, &ch->Luck, &ch->wsps };
        const int kinds[] = { STR, DEX, CON, WIS, INT_, CHA, MOVP, ENDU, MOR, LUCK, WSPS };
        for (int i = 0; i < LOCAL_ADMIN_ATTRIBUTE_COUNT; ++i)
        {
            *fields[i] = static_cast<short>(state.attributes[i]);
            ch->SendCharInfoBasic(kinds[i], state.attributes[i]);
        }
        ch->SetLevel(state.level);
        ch->SetExperienceStep(NPC_Lev_Ref[state.level].nStep);
        ch->Exp = state.experience;
        ch->SetReservedPoint(state.points);
        ch->SendCharInfoBasic(EXP, ch->Exp);
        ch->SendCharInfoBasic(LVPOINT, state.points);
        ResetAbility(ch);
        SendLevelUpMsgAndPoint(ch);
    }

    int EmptySlots(CHARLIST* ch)
    {
        ItemAttr* inventory = &ch->inv[0][0][0];
        int count = 0;
        for (int i = 0; i < 72; ++i)
            if (!inventory[i].item_no) ++count;
        return count;
    }

    bool InsertItem(CHARLIST* ch, const ItemAttr& item)
    {
        int a = 0, b = 0, c = 0;
        if (!SearchInv(ch->inv, a, b, c)) return false;
        ch->inv[a][b][c] = item;
        SendServerEachItem(&ch->inv[a][b][c], ch);
        return true;
    }

    bool SetEmblems(CHARLIST* ch, DWORD desired)
    {
        const DWORD current = GetItemMuchByMuchItem(ch, 10057);
        if (desired == current) return true;
        ItemAttr* inventory = &ch->inv[0][0][0];
        if (desired > current)
        {
            for (int i = 0; i < 72; ++i)
            {
                if (inventory[i].item_no != 10057) continue;
                inventory[i].attr[IATTR_MUCH] += desired - current;
                SendServerEachItem(&inventory[i], ch);
                return true;
            }
            if (!EmptySlots(ch)) return false;
            ItemAttr item = GenerateItem(10057, IATTR_MUCH, desired - current);
            return InsertItem(ch, item);
        }
        DWORD remaining = current - desired;
        for (int i = 0; i < 72 && remaining; ++i)
        {
            if (inventory[i].item_no != 10057) continue;
            POS position;
            SetItemPos(INV, i / 24, (i / 8) % 3, i % 8, &position);
            const DWORD removed = min(remaining, inventory[i].attr[IATTR_MUCH]);
            remaining -= removed;
            if (removed == inventory[i].attr[IATTR_MUCH])
                SendDeleteItem(&inventory[i], &position, ch);
            else
            {
                inventory[i].attr[IATTR_MUCH] -= removed;
                SendServerEachItem(&inventory[i], ch);
            }
        }
        return true;
    }

    bool PrepareItem(const LocalAdminRequest& request, ItemDraft& draft, char* message, size_t size)
    {
        const int type = request.itemNumber / 1000;
        const int number = request.itemNumber % 1000;
        if (type < 0 || type > 10 || number <= 0 || number >= MAX_ITEM_NUM ||
            request.quantity < 1 || request.quantity > 1000000 ||
            request.durability < 0 || request.durability > 65535 ||
            request.grade < 0 || request.grade > 15 ||
            request.highItem < 0 || request.highItem > H_LV_LEGEND_ITEM ||
            request.dynamicEffect < 0 || request.dynamicEffect > 1)
        {
            strcpy_s(message, size, "Item ou valores fora da faixa suportada.");
            return false;
        }
        CItem* definition = ItemUnit(request.itemNumber);
        if (!definition || !definition->GetHanName() || !definition->GetHanName()[0])
        {
            strcpy_s(message, size, "Item inexistente nesta revisao.");
            return false;
        }
        const bool stackable = definition->GetRbutton() == DIVIDE_ITEM;
        if (!stackable && request.quantity > 72)
        {
            strcpy_s(message, size, "Itens individuais limitados a 72 por operacao.");
            return false;
        }
        if (request.highItem == H_LV_LEGEND_ITEM)
        {
            const int legendGroup = definition->GetLegendGroup();
            if (stackable || request.dynamicEffect || !definition->GetLegendAble() ||
                legendGroup < 1 || legendGroup >= MAX_ITEM_LEGEND)
            {
                strcpy_s(message, size, "Este item nao aceita Legend.");
                return false;
            }

            if (request.legendVariant)
            {
                const int variant = request.legendVariant;
                const int effect = variant > 0 && variant < MAX_ITEM_LEGEND_SUB &&
                    variant <= RareEM.GetLegendVariantCount(legendGroup) ?
                    RareEM.GetLegendBasicRareKind(legendGroup, variant, definition->GetWearAble()) : 0;
                if (effect <= 0 || effect >= MAX_ITEM_RARE || request.grade < 3 || request.grade > 8)
                {
                    strcpy_s(message, size, "Variante Legend invalida para este item. Escolha grau de 3 a 8.");
                    return false;
                }
                draft.item = GenerateItem(request.itemNumber);
                ItemMgr.MakeRareAttr(draft.item.attr[IATTR_RARE_MAIN], request.grade, effect,
                    legendGroup, variant, H_LV_LEGEND_ITEM, false);
            }
            else draft.item = ItemMgr.GiveLegendItem(request.itemNumber);
            RareMain legend = {};
            memcpy(&legend, &draft.item.attr[IATTR_RARE_MAIN], sizeof(legend));
            if (!draft.item.item_no || legend.iHighLevel != H_LV_LEGEND_ITEM ||
                !legend.grade || !legend.soksung1 || !legend.soksung2 || !legend.soksung3)
            {
                strcpy_s(message, size, "Nao foi possivel gerar um Legend com as tabelas do servidor.");
                return false;
            }
            draft.copies = request.quantity;
            if (request.durability)
                draft.item.attr[IATTR_DURATION] = MAKELONG(request.durability, request.durability);
            sprintf_s(message, size, "%s | ID %d\r\nLegend | Grau %d | Quantidade %d\r\nEfeito %d | Grupo %d | Variante %d\r\nDurabilidade %u / %u",
                definition->GetHanName(), request.itemNumber, int(legend.grade), draft.copies,
                int(legend.soksung1), int(legend.soksung2), int(legend.soksung3),
                LOWORD(draft.item.attr[IATTR_DURATION]), HIWORD(draft.item.attr[IATTR_DURATION]));
            return true;
        }

        draft.item = GenerateItem(request.itemNumber);
        if (!draft.item.item_no) return false;
        draft.copies = stackable ? 1 : request.quantity;
        if (stackable) draft.item.attr[IATTR_MUCH] = request.quantity;
        else if (request.durability)
            draft.item.attr[IATTR_DURATION] = MAKELONG(request.durability, request.durability);
        if (request.grade)
        {
            for (int i = 0; i < 3; ++i)
            {
                const int effect = request.effects[i];
                const int limit = request.dynamicEffect ? MAX_ITEM_FUNCTION : MAX_ITEM_RARE;
                if (effect < 0 || effect >= limit || (i == 0 && !effect) ||
                    (request.dynamicEffect && i > 0 && effect) ||
                    (effect && !(request.dynamicEffect ? ItemFunction[effect].Name[0] : ItemRare[effect].LocalName[0])))
                {
                    strcpy_s(message, size, "Efeito inexistente no catalogo do servidor.");
                    return false;
                }
            }
            if ((!request.dynamicEffect && type != 6 && type != 8 && type != 9) ||
                (request.dynamicEffect && definition->GetRbutton() != USE_ITEM) ||
                !ItemMgr.MakeRareAttr(draft.item.attr[IATTR_RARE_MAIN], request.grade,
                    request.effects[0], request.effects[1], request.effects[2],
                    request.highItem, request.dynamicEffect != 0))
            {
                strcpy_s(message, size, "Raridade incompativel com o item ou efeito selecionado.");
                return false;
            }
        }
        sprintf_s(message, size, "%s | ID %d\r\nQuantidade %d | Grau %d\r\nEfeitos %d / %d / %d | %s\r\nDurabilidade %u / %u\r\nEspacos necessarios: %d",
            definition->GetHanName(), request.itemNumber, request.quantity, request.grade,
            request.grade ? request.effects[0] : 0, request.grade ? request.effects[1] : 0, request.grade ? request.effects[2] : 0,
            request.grade && request.dynamicEffect ? "Dinamico" : "Estatico",
            LOWORD(draft.item.attr[IATTR_DURATION]), HIWORD(draft.item.attr[IATTR_DURATION]), draft.copies);
        return true;
    }
}

void HandleLocalAdmin(int cn, const void* payload, int length)
{
    if (cn < DRAGON_CONNECTIONS_START || cn >= DRAGON_MAX_CONNECTIONS ||
        connections[cn].state != CONNECT_JOIN || length != sizeof(LocalAdminRequest)) return;
    LocalAdminRequest request;
    memcpy(&request, payload, sizeof(request));
    if (request.version != LOCAL_ADMIN_VERSION) return;
    CHARLIST* ch = CheckServerId(cn);
    if (!ch) return;
    LocalAdminReply reply = {};
    reply.version = LOCAL_ADMIN_VERSION;
    reply.operation = request.operation;
    reply.requestId = request.requestId;
    char account[32] = {};
    GetPrivateProfileStringA("local_admin", "account", "", account, sizeof(account), ".\\MapServer.ini");
    bool authorized = account[0] && _stricmp(account, connections[cn].id) == 0;
    const char* familyKeys[] = { "account1", "account2", "account3" };
    for (const char* key : familyKeys)
    {
        GetPrivateProfileStringA("local_admin", key, "", account, sizeof(account), "..\\family-gm.ini");
        if (account[0] && _stricmp(account, connections[cn].id) == 0)
            authorized = true;
    }
    const bool eventOperator = !authorized &&
        GetPrivateProfileIntA("event_operators", connections[cn].id, 0, "..\\family-gm.ini") == 1;
    authorized = (authorized || eventOperator) && GetPrivateProfileIntA("local_admin", "enabled", 0, ".\\MapServer.ini");
    const bool mutation = request.operation != LA_READ && request.operation != LA_PREVIEW &&
        request.operation != LA_SESSION && request.operation != LA_TELEPORT &&
        request.operation != LA_ANNOUNCE && request.operation != LA_GATHER &&
        request.operation != LA_SUMMON && request.operation != LA_SUMMON_LIST &&
        request.operation != LA_SUMMON_REMOVE && request.operation != LA_MAP_SEARCH;
    LP_SERVER_DATA database = g_pServerTable->GetOwnDBDemonData();
    if (authorized) tacticSaves.erase(cn);
    bool changed = false;
    request.target[sizeof(request.target) - 1] = 0;
    request.announcement[sizeof(request.announcement) - 1] = 0;
    const bool eventOperation = request.operation == LA_READ || request.operation == LA_SESSION ||
        request.operation == LA_SESSION_ALL || request.operation == LA_ANNOUNCE || request.operation == LA_ANNOUNCE_ALL ||
        request.operation == LA_MAP_SEARCH || request.operation == LA_TELEPORT || request.operation == LA_GATHER ||
        request.operation == LA_SUMMON || request.operation == LA_SUMMON_LIST || request.operation == LA_SUMMON_REMOVE;
    if (!authorized || (eventOperator && !eventOperation))
        strcpy_s(reply.message, "Conta sem acesso ao painel GM local.");
    else if (mutation && request.operation != LA_INSPECT && request.operation != LA_LEGEND_CATALOG &&
        request.operation != LA_RECOVER && request.operation != LA_TRAINING && request.operation != LA_TRAINING_STATS &&
        request.operation != LA_SESSION_ALL && request.operation != LA_ANNOUNCE_ALL && request.operation != LA_AUDIT && (!database || !database->dwConnectionIndex || !CanSaveUserData(ch, 4)))
        strcpy_s(reply.message, "Personagem ou banco ainda indisponivel para salvar. Tente novamente.");
    else switch (request.operation)
    {
    case LA_READ:
        drafts.erase(cn);
        reply.success = 1;
        strcpy_s(reply.message, "Dados recebidos do servidor.");
        break;
    case LA_REVIVE:
        if (ch->viewtype != VIEWTYPE_GHOST_ || !ch->IsDead())
            strcpy_s(reply.message, "O personagem precisa estar morto e em forma de fantasma.");
        else
        {
            // Same full-resource resurrection as the revival item; consume no item.
            SkillMgr.CharacterToAlive(ch, 1);
            reply.success = 1;
            changed = true;
            strcpy_s(reply.message, "Personagem revivido instantaneamente.");
        }
        break;
    case LA_INSPECT:
        {
            CHARLIST* target = request.target[0] ? CheckServerName(request.target) : ch;
            if (!target || connections[target->GetServerID()].state != CONNECT_JOIN)
            {
                strcpy_s(reply.message, "Personagem nao encontrado no mapa atual.");
                break;
            }
            AppendDetails(reply, "%s | Nivel %d | Estado %u | HP %d/%d | MP %d/%d\r\n",
                target->Name, target->GetLevel(), unsigned(target->GetState()), target->Hp, target->HpMax, target->Mana, target->ManaMax);
            AppendDetails(reply, "Veneno: %u por ciclo | Amplificacao magica: %d | Defesa fisica C/M/L: %d/%d/%d\r\n",
                unsigned(target->GetPoisonedPoint()), target->GetMagicAmplify(),
                target->GetShortDefense(), target->GetMiddleDefense(), target->GetLongDefense());
            const int kinds[] = { STR, DEX, CON, WIS, INT_, CHA, MOVP, ENDU, MOR, LUCK, WSPS };
            const char* labels[] = { "STR", "DEX", "CON", "WIS", "INT", "CHA", "MOVP", "ENDU", "MOR", "LUCK", "WSPS" };
            for (int i = 0; i < LOCAL_ADMIN_ATTRIBUTE_COUNT; ++i)
                AppendDetails(reply, "%s %d  ", labels[i], target->GetAbility(kinds[i]));
            AppendDetails(reply, "\r\nTaticas (ID: nivel / XP / etapa)\r\n");
            for (int i = 0; i < NO_TAC; ++i)
                AppendDetails(reply, "%d: %u / %lu / %d%s", TACTICS_Crapple + i, unsigned(target->Skill[TACTICS_Crapple + i]),
                    target->tac_skillEXP[i], target->GetTacticsStep(i), i % 3 == 2 ? "\r\n" : "   ");
            AppendDetails(reply, "\r\nEquipamento (espaco: ID e nome)\r\n");
            for (size_t i = 0; i < sizeof(target->equip) / sizeof(target->equip[0]); ++i)
            {
                const int item = target->equip[i].item_no;
                CItem* definition = item ? ItemUnit(item) : NULL;
                if (definition) AppendDetails(reply, "%u: %d %.64s\r\n", unsigned(i), item, definition->GetHanName());
            }
            reply.success = 1;
            strcpy_s(reply.message, "Inspecao do personagem recebida.");
        }
        break;
    case LA_LEGEND_CATALOG:
        {
            const int type = request.itemNumber / 1000, number = request.itemNumber % 1000;
            CItem* item = type >= 0 && type <= 10 && number > 0 && number < MAX_ITEM_NUM ? ItemUnit(request.itemNumber) : NULL;
            if (!item || !item->GetLegendAble() || RareEM.GetLegendVariantCount(item->GetLegendGroup()) <= 0)
            {
                strcpy_s(reply.message, "Item sem grupo Legend valido.");
                break;
            }
            const int group = item->GetLegendGroup();
            AppendDetails(reply, "%.64s | Grupo %d\r\nVariantes disponiveis (efeito e valor base nos graus 3 a 8)\r\n", item->GetHanName(), group);
            for (int v = 1; v < MAX_ITEM_LEGEND_SUB && v <= RareEM.GetLegendVariantCount(group); ++v)
            {
                const int effect = RareEM.GetLegendBasicRareKind(group, v, item->GetWearAble());
                if (effect <= 0 || effect >= MAX_ITEM_RARE) continue;
                AppendDetails(reply, "%d: %.50s (%d)", v, ItemRare[effect].LocalName, effect);
                for (int grade = 3; grade <= 8; ++grade)
                    AppendDetails(reply, " %d", ItemRare[effect].GRADE[grade]);
                AppendDetails(reply, "\r\n");
            }
            AppendDetails(reply, "Equipamento atual com o mesmo tipo de encaixe:\r\n");
            for (size_t i = 0; i < sizeof(ch->equip) / sizeof(ch->equip[0]); ++i)
            {
                const ItemAttr& equipped = ch->equip[i];
                CItem* current = equipped.item_no ? ItemUnit(equipped.item_no) : NULL;
                if (!current || current->GetWearAble() != item->GetWearAble()) continue;
                RareMain rare = {};
                memcpy(&rare, &equipped.attr[IATTR_RARE_MAIN], sizeof(rare));
                const unsigned effect = rare.soksung1, grade = rare.grade;
                const int bonus = !rare.IsDynamicRare && effect > 0 && effect < MAX_ITEM_RARE &&
                    grade > 0 && grade < sizeof(ItemRare[0].GRADE) / sizeof(ItemRare[0].GRADE[0]) ? ItemRare[effect].GRADE[grade] : 0;
                AppendDetails(reply, "%.50s | Grau %u | Efeito %u | Valor base %d\r\n",
                    current->GetHanName(), grade, effect, bonus);
            }
            reply.success = 1;
            strcpy_s(reply.message, "Catalogo Legend recebido.");
        }
        break;
    case LA_TRAINING_STATS:
        {
            for (int i = NPC_LIST_START; i < MAX_NPC_LIST; ++i)
            {
                const CHARLIST& dummy = NPCList[i];
                if (!dummy.localAdminTraining || dummy.bAlive != ALIVE_) continue;
                const DWORD hits = dummy.localAdminTrainingHits;
                AppendDetails(reply, "Alvo #%lu | golpes %lu | total %llu | media %llu\r\n",
                    dummy.localAdminSummonGroup, hits, dummy.localAdminTrainingDamage,
                    hits ? dummy.localAdminTrainingDamage / hits : 0ULL);
                if (!hits) continue;
                AppendDetails(reply, "Ultimos golpes (mais antigo primeiro):");
                for (DWORD hit = hits > 8 ? hits - 8 : 0; hit < hits; ++hit)
                    AppendDetails(reply, " %lu", dummy.localAdminTrainingRecent[hit % 8]);
                AppendDetails(reply, "\r\n");
            }
            if (!reply.details[0]) AppendDetails(reply, "Nenhum alvo ativo no mapa.");
            reply.success = 1;
            strcpy_s(reply.message, "Medicao de dano recebida.");
        }
        break;
    case LA_RECOVER:
        if (request.progressionIndex == 2 && ch->viewtype == VIEWTYPE_GHOST_ && !ch->IsReporter())
        {
            SkillMgr.CharacterToAlive(ch);
            reply.success = 1;
        }
        else if (request.progressionIndex == 0 && ch->Hp > 0)
        {
            ch->Hp = ch->HpMax;
            ch->Mana = ch->ManaMax;
            ch->SendCharInfoBasic(HP, ch->Hp);
            ch->SendCharInfoBasic(MP, ch->Mana);
            reply.success = 1;
        }
        else if (request.progressionIndex == 1 && ch->Hp > 0)
        {
            for (int i = 1; i <= 4; ++i) ch->ClearCurse(i);
            ch->SetPoisonedPoint();
            ch->SetPoisonedCount();
            SendMagicCommand(ch, SET_CUREBLIND, 0, 0);
            SendCharacterCondition(ch, 0);
            reply.success = 1;
        }
        strcpy_s(reply.message, reply.success ? "Recuperacao aplicada." : "Recuperacao indisponivel no estado atual.");
        break;
    case LA_SESSION_ALL:
        {
            AppendMapRoster(reply);
            LP_SERVER_DATA own = g_pServerTable->GetOwnServerData();
            if (own)
            {
                LocalAdminMapRoster roster = {};
                roster.originPort = own->wPort;
                roster.ticket = ++nextRosterTicket;
                if (!roster.ticket) roster.ticket = ++nextRosterTicket;
                const RosterRequest query = { roster.ticket, connections[cn].dwIDForAgent,
                    request.requestId, GetTickCount() };
                rosterRequests[cn] = query;
                for (LP_SERVER_DATA map = g_pServerTable->GetServerListHead(); map; map = map->pNextServerData)
                    if (map->wPort != own->wPort && map->dwServerType == SERVER_TYPE_MAP && map->dwConnectionIndex)
                        SendRoster(map->wPort, roster);
            }
            reply.success = 1;
            strcpy_s(reply.message, "Mapa atual recebido. Outros mapas aparecem conforme responderem.");
        }
        break;
    case LA_ANNOUNCE_ALL:
        if (request.announcement[0])
        {
            t_packet notice = {};
            notice.h.header.type = CMD_LOCAL_ADMIN_MAP_NOTICE;
            notice.h.header.size = sizeof(request.announcement);
            memcpy(&notice.u, request.announcement, sizeof(request.announcement));
            HandleLocalAdminMapNotice(request.announcement, sizeof(request.announcement));
            for (LP_SERVER_DATA map = g_pServerTable->GetServerListHead(); map; map = map->pNextServerData)
            {
                if (map == g_pServerTable->GetOwnServerData() || map->dwServerType != SERVER_TYPE_MAP || !map->dwConnectionIndex) continue;
                g_pServerTable->SendRajaPacketToOtherMapServer(map->wPort, reinterpret_cast<char*>(&notice),
                    notice.h.header.size + sizeof(t_header));
            }
            reply.success = 1;
            strcpy_s(reply.message, "Aviso enviado aos mapas conectados.");
        }
        break;
    case LA_AUDIT:
        {
            FILE* file = fopen(auditPath, "r");
            if (file)
            {
                char line[256];
                std::deque<std::string> last;
                while (fgets(line, sizeof(line), file))
                {
                    last.push_back(line);
                    if (last.size() > 16) last.pop_front();
                }
                fclose(file);
                for (auto it = last.rbegin(); it != last.rend(); ++it)
                {
                    std::string entry = *it;
                    if (!entry.empty() && entry.back() == '\n') entry.pop_back();
                    AppendDetails(reply, "%s\r\n", entry.c_str());
                }
            }
            reply.success = 1;
            if (!reply.details[0]) AppendDetails(reply, "Nenhum registro neste mapa.");
            strcpy_s(reply.message, "Historico persistente deste mapa.");
        }
        break;
    case LA_MAP_SEARCH:
        {
            request.state.map[sizeof(request.state.map) - 1] = 0;
            CharUpperA(request.state.map);
            size_t used = 0;
            for (int i = 0; i < MAX_MAP_; ++i)
            {
                if (!MapInfo[i].mapfile[0] || !MapInfo[i].port) continue;
                char code[sizeof(MapInfo[i].mapfile)];
                char name[sizeof(MapInfo[i].mapname)];
                strcpy_s(code, MapInfo[i].mapfile);
                strcpy_s(name, MapInfo[i].mapname);
                CharUpperA(code);
                CharUpperA(name);
                if (!strstr(code, request.state.map) && !strstr(name, request.state.map)) continue;
                const size_t width = strlen(code);
                if (used + width + 3 >= sizeof(reply.message)) break;
                if (used) { memcpy(reply.message + used, ", ", 2); used += 2; }
                memcpy(reply.message + used, code, width);
                used += width;
            }
            if (!used) strcpy_s(reply.message, "Nenhum mapa encontrado.");
            reply.success = 1;
        }
        break;
    case LA_TELEPORT:
        if (!memchr(request.state.map, 0, sizeof(request.state.map)) || !request.state.map[0])
            strcpy_s(reply.message, "Informe o codigo do mapa.");
        else if (_stricmp(request.state.map, MapName) != 0)
        {
            const t_MapInfo* destination = GetMapInfoByMapName(request.state.map);
            const int x = destination ? destination->nSaveX : 0;
            const int y = destination ? destination->nSaveY : 0;
            if (destination && destination->port && x > 0 && y > 0 && x <= 32767 && y <= 32767 &&
                MapMove(cn, destination->mapfile, x, y))
            {
                reply.success = 1;
                strcpy_s(reply.message, "Transferencia solicitada ao servidor de destino.");
            }
            else strcpy_s(reply.message, "Mapa sem destino de entrada ou transferencia indisponivel.");
        }
        else
        {
            int x = request.state.mapX, y = request.state.mapY;
            if (x < 1 || x >= g_Map.file.wWidth - 1 || y < 1 || y >= g_Map.file.wHeight - 1 ||
                !IsMovable(x, y))
                strcpy_s(reply.message, "Escolha uma posicao transitavel no mapa atual.");
            else
            {
                QuestTeleport(cn, x, y);
                reply.success = 1;
                strcpy_s(reply.message, "Teleporte aplicado.");
            }
        }
        break;
    case LA_SESSION:
    case LA_ANNOUNCE:
    case LA_GATHER:
        {
            request.announcement[sizeof(request.announcement) - 1] = 0;
            if (request.operation == LA_ANNOUNCE && !request.announcement[0])
            {
                strcpy_s(reply.message, "Digite o aviso antes de enviar.");
                break;
            }
            int x = ch->X / TILE_SIZE, y = ch->Y / TILE_SIZE;
            if (request.operation == LA_GATHER &&
                (x < 1 || x >= g_Map.file.wWidth - 1 || y < 1 || y >= g_Map.file.wHeight - 1 || !IsMovable(x, y)))
            {
                strcpy_s(reply.message, "Va para uma posicao transitavel antes de reunir jogadores.");
                break;
            }
            int count = 0;
            size_t used = 0;
            for (int player = DRAGON_CONNECTIONS_START; player < DRAGON_MAX_CONNECTIONS; ++player)
            {
                if (connections[player].state != CONNECT_JOIN) continue;
                CHARLIST* target = CheckServerId(player);
                if (!target) continue;
                if (request.operation == LA_ANNOUNCE) SendBBS2(request.announcement, static_cast<short>(player));
                if (request.operation == LA_GATHER && player != cn) QuestTeleport(player, x, y);
                if (request.operation == LA_SESSION)
                {
                    const size_t nameLength = strlen(target->Name);
                    if (used + nameLength + 3 < sizeof(reply.message))
                    {
                        if (used) { memcpy(reply.message + used, ", ", 2); used += 2; }
                        memcpy(reply.message + used, target->Name, nameLength);
                        used += nameLength;
                    }
                }
                if (request.operation != LA_GATHER || player != cn) ++count;
            }
            if (request.operation == LA_SESSION)
                sprintf_s(reply.message + used, sizeof(reply.message) - used, " (%d no mapa)", count);
            else
                sprintf_s(reply.message, "%d jogadores no mapa receberam a operacao.", count);
            reply.success = 1;
        }
        break;
    case LA_SUMMON_LIST:
        {
            std::map<DWORD, int> groups;
            for (int i = NPC_LIST_START; i < MAX_NPC_LIST; ++i)
                if (NPCList[i].localAdminSummonGroup && NPCList[i].bAlive == ALIVE_)
                    ++groups[NPCList[i].localAdminSummonGroup];
            size_t used = sprintf_s(reply.message, "Grupos ativos: %u. ", static_cast<unsigned>(groups.size()));
            for (const auto& group : groups)
            {
                char entry[32];
                const int length = sprintf_s(entry, "#%lu (%d) ", group.first, group.second);
                if (used + length >= sizeof(reply.message)) break;
                memcpy(reply.message + used, entry, length + 1);
                used += length;
            }
            reply.success = 1;
        }
        break;
    case LA_SUMMON_REMOVE:
        {
            const bool event = request.progressionIndex == 3;
            const DWORD playerId = connections[cn].dwIDForAgent;
            const auto active = eventGroups.find(playerId);
            if (event && active != eventGroups.end() && active->second != request.previewToken)
            {
                strcpy_s(reply.message, "O grupo informado nao corresponde ao seu evento ativo.");
                break;
            }
            if (event && active == eventGroups.end())
            {
                reply.success = 1;
                strcpy_s(reply.message, "Nenhum evento ativo neste servidor.");
                break;
            }
            int removed = 0;
            for (int i = NPC_LIST_START; i < MAX_NPC_LIST; ++i)
            {
                if (!request.previewToken || NPCList[i].localAdminSummonGroup != request.previewToken ||
                    NPCList[i].bAlive != ALIVE_) continue;
                DeleteNpc(i);
                ++removed;
            }
            if (event) eventGroups.erase(active);
            reply.success = event || removed > 0;
            sprintf_s(reply.message, "Grupo #%lu: %d monstros removidos.", static_cast<unsigned long>(request.previewToken), removed);
        }
        break;
    case LA_PRESET:
        {
            DWORD experiences[NO_TAC] = {};
            bool valid = ValidAttributes(request.state) &&
                memchr(request.state.character, 0, sizeof(request.state.character)) &&
                strcmp(request.state.character, ch->Name) == 0 &&
                request.state.baseClass == ch->Class &&
                request.state.dualClass == ch->GetDualClass() &&
                request.state.dualStep == ch->GetClassStep();
            int combat = ch->GetCombatPoint();
            for (int i = 0; i < 36; ++i) combat += ch->GetCombatLevel(LIGHTNING_BOOM + i);
            if (valid && combat > NPC_Lev_Ref[request.state.level].nCspMax) valid = false;
            for (int i = 0; valid && i < NO_TAC; ++i)
            {
                const int level = request.state.skills[TACTICS_Crapple + i];
                valid = GetTacticStartingExperience(level, experiences[i]);
                if (valid && request.state.tacticStep[i] == NPC_Lev_Ref[level].nStep &&
                    request.state.tacticExp[i] >= experiences[i] &&
                    request.state.tacticExp[i] <= static_cast<DWORD>(NPC_Lev_Ref[level].nMaxExp))
                    experiences[i] = request.state.tacticExp[i];
            }
            for (int i = 0; valid && i < 200; ++i)
                valid = request.state.wizard[i] <= 1 && request.state.priest[i] <= 1;
            if (!valid)
            {
                strcpy_s(reply.message, "Preset incompativel com personagem, classe, etapa ou tabela de progressao.");
                break;
            }
            ApplyAttributes(ch, request.state);
            for (int i = 0; i < NO_TAC; ++i)
                SetTactic(ch, i, request.state.skills[TACTICS_Crapple + i], experiences[i]);
            memcpy(ch->Ws, request.state.wizard, sizeof(request.state.wizard));
            memcpy(ch->Ps, request.state.priest, sizeof(request.state.priest));
            reply.success = 1;
            changed = true;
        }
        break;
    case LA_TRAINING:
    case LA_SUMMON:
        if ((request.progressionIndex == 1 || request.progressionIndex == 3) &&
            (!memchr(request.state.map, 0, sizeof(request.state.map)) ||
             !memchr(request.state.character, 0, sizeof(request.state.character)) ||
             _stricmp(request.state.map, MapName) || strcmp(request.state.character, ch->Name)))
            strcpy_s(reply.message, "Ondas interrompidas: personagem ou mapa mudou.");
        else if (request.itemNumber < 0 || request.itemNumber >= Num_Of_NPC_Generation ||
            !NPC_Gen_Ref[request.itemNumber].Name[0] || NPC_Gen_Ref[request.itemNumber].nHpMax <= 0 ||
            NPC_Gen_Ref[request.itemNumber].SprNO <= 0 || request.quantity < 1 || request.quantity > 20)
            strcpy_s(reply.message, "Selecione um monstro valido e quantidade de 1 a 20.");
        else if (summonTimes.count(cn) && GetTickCount() - summonTimes[cn] < 3000)
            strcpy_s(reply.message, "Aguarde 3 segundos entre invocacoes.");
        else {
            summonTimes[cn] = GetTickCount();
            const bool training = request.operation == LA_TRAINING;
            const bool event = request.progressionIndex == 3 && !training;
            const DWORD playerId = connections[cn].dwIDForAgent;
            DWORD group = event && request.summonGroup && eventGroups.count(playerId) &&
                eventGroups[playerId] == request.summonGroup ? request.summonGroup : 0;
            const bool newGroup = !group;
            if (!group) group = ++nextSummonGroup;
            if (!group) group = ++nextSummonGroup;
            int centerX = event ? request.state.mapX : ch->X / TILE_SIZE;
            int centerY = event ? request.state.mapY : ch->Y / TILE_SIZE;
            if (centerX < 1 || centerY < 1 || centerX >= g_Map.file.wWidth - 1 ||
                centerY >= g_Map.file.wHeight - 1 || !IsMovable(centerX, centerY))
            {
                strcpy_s(reply.message, "Ponto de invocacao invalido.");
                break;
            }
            const int quantity = training ? 1 : request.quantity;
            int created = 0, upgraded = 0;
            for (int attempt = 0; attempt < 81 && created < quantity; ++attempt) {
                int x = centerX + (attempt % 9) - 4;
                int y = centerY + (attempt / 9) - 4;
                if (x < 1 || y < 1 || x >= g_Map.file.wWidth - 1 || y >= g_Map.file.wHeight - 1) continue;
                if (!IsMovable(x, y)) continue;
                const int slot = GetAliveNPCList();
                if (slot < 0) break;
                if (NPC_Create(slot, request.itemNumber, x, y, -1, 0, 9)) {
                    NPCList[slot].localAdminSummonGroup = group;
                    ++created;
                    if (training)
                    {
                        NPCList[slot].localAdminTraining = true;
                        NPCList[slot].ChairNum = 0;
                        NPCList[slot].tamingable = 0;
                        memset(NPCList[slot].inv, 0, sizeof(NPCList[slot].inv));
                    }
                    else upgraded += MaximizeSummonDropRarity(&NPCList[slot]);
                }
            }
            reply.success = created > 0;
            reply.summonGroup = created ? group : 0;
            if (created && event)
            {
                eventGroups[playerId] = group;
                if (newGroup && request.announcement[0])
                    HandleLocalAdminMapNotice(request.announcement, sizeof(request.announcement));
            }
            sprintf_s(reply.message, "Grupo #%lu: %d de %d invocados. Equipamentos convertidos em raros: %d. Sem respawn.", group, created, request.quantity, upgraded);
            MyLog(LOG_NORMAL, "LOCAL GM summon character=%s monster=%d requested=%d created=%d", ch->Name, request.itemNumber, request.quantity, created);
        }
        break;
    case LA_DUAL:
        if (request.state.dualClass < 0 || request.state.dualClass >= MAX_CLASS ||
            request.state.dualStep < 0 || request.state.dualStep > 5 ||
            request.state.fame < 0 || request.state.fame > 32767 ||
            request.state.ladder < 0 || request.state.combatPoints < 0 || request.state.combatPoints > 127)
            strcpy_s(reply.message, "Dual: classe 0-4, etapa 0-5, fama 0-32767, CSP 0-127, ladder positivo.");
        else if (ch->quick[5].item_no && request.state.dualStep == 0)
            strcpy_s(reply.message, "Remova o symbol pelo painel antes de voltar a dual 0.");
        else if (!ValidCombatTotal(ch, request.state.combatPoints))
            strcpy_s(reply.message, "CSP mais niveis de combate excedem o limite do nivel atual. Aumente o nivel do personagem primeiro.");
        else if (ch->quick[5].item_no && !g_CSymbolMgr.GetItemNumber(ch->Class, request.state.dualClass))
            strcpy_s(reply.message, "Nao existe symbol para essa combinacao de classes.");
        else {
            if (ch->IsDual()) g_pDualManager->DelMember(cn);
            ch->SetDualClass(request.state.dualClass);
            ch->SetClassStep(request.state.dualStep);
            ch->fame = static_cast<short>(request.state.fame);
            ch->LadderScore = request.state.ladder;
            ch->SetCombatPoint(request.state.combatPoints);
            if (ch->IsDual()) g_pDualManager->AddMember(cn);
            g_pDualManager->SendDualChange(cn);
            if (ch->quick[5].item_no) g_CSymbolMgr.MakeSymbolItem(ch, ch->quick[5].attr[0]);
            ResetAbility(ch);
            reply.success = 1; changed = true;
        }
        break;
    case LA_SYMBOL:
        if (request.grade < 0 || request.grade > MAX_SYMBOL_LEVEL)
            strcpy_s(reply.message, "Symbol: grau 0 remove; graus 1-5 criam ou atualizam.");
        else if (request.grade && (!ch->IsDual() || !g_CSymbolMgr.GetItemNumber(ch->Class, ch->GetDualClass())))
            strcpy_s(reply.message, "Defina uma dual class com symbol valido antes de cria-lo.");
        else {
            memset(&ch->quick[5], 0, sizeof(ItemAttr));
            if (request.grade) g_CSymbolMgr.MakeSymbolItem(ch, request.grade);
            ResetAbility(ch);
            reply.success = 1; changed = true;
        }
        break;
    case LA_UNDO_TACTIC:
        {
            auto previous = tacticUndo.find(cn);
            if (previous == tacticUndo.end() || previous->second.character != ch->Name ||
                previous->second.playerId != connections[cn].dwIDForAgent)
            {
                strcpy_s(reply.message, "Nenhuma edicao de tatica para desfazer nesta sessao.");
                break;
            }
            const TacticUndo& edit = previous->second;
            if (ch->GetTacticsStep(edit.index) != edit.newStep || ch->tac_skillEXP[edit.index] != edit.newExperience)
            {
                strcpy_s(reply.message, "A tatica mudou desde a edicao. Atualize o painel.");
                break;
            }
            ch->SetTacticsStep(edit.index, edit.oldStep);
            ch->tac_skillEXP[edit.index] = edit.oldExperience;
            ch->ConvertTacticsLevel(edit.index);
            ch->SendCharInfoTactic(edit.index);
            tacticUndo.erase(previous);
            reply.success = 1;
            changed = true;
        }
        break;
    case LA_PROGRESSION:
        {
            const int kind = request.progressionKind, index = request.progressionIndex;
            const int level = request.progressionLevel;
            if (kind == 0 && index >= 0 && index < NO_TAC) {
                DWORD experience = 0;
                if (GetTacticStartingExperience(level, experience)) {
                    TacticUndo edit = { ch->Name, connections[cn].dwIDForAgent, index, ch->GetTacticsStep(index),
                        NPC_Lev_Ref[level].nStep, ch->tac_skillEXP[index], experience };
                    SetTactic(ch, index, level, experience);
                    tacticUndo[cn] = edit;
                    reply.success = 1;
                }
                else strcpy_s(reply.message, "Nivel de tatica sem faixa de experiencia valida na tabela do servidor.");
            }
            else if (kind == 1 && index >= 0 && index <= 112 && !(index >= 88 && index <= 100) &&
                level >= 0 && level <= 1) {
                ch->Skill[index] = static_cast<BYTE>(level); reply.success = 1;
            }
            else if (kind == 5 && index >= 0 && index < 36 && level >= 0 && level <= MAX_COMBAT_LEVEL && ValidCombatTotal(ch, ch->GetCombatPoint(), index, level)) {
                ch->SetCombatLevel(LIGHTNING_BOOM + index, level); reply.success = 1;
            }
            else if (kind == 2 && index >= 0 && index < 45 && level >= 0 && level <= 100 &&
                request.progressionExp < 10000 && request.progressionRare <= 4095 &&
                (level < 100 || request.progressionExp == 0)) {
                ch->skillexp[index].skillexp = level * 10000 + request.progressionExp;
                ch->skillexp[index].rare = request.progressionRare;
                SendStartSkillExp(index, ch->skillexp[index], connections, cn);
                reply.success = 1;
            }
            else if ((kind == 3 || kind == 4) && index >= 0 && index < 200 && level >= 0 && level <= 1) {
                (kind == 3 ? ch->Ws : ch->Ps)[index] = static_cast<BYTE>(level); reply.success = 1;
            }
            else if (kind == 6 && index >= 0 && index < 7 && level >= 0 && level <= (index == 6 ? 5 : 32767)) {
                const int scriptIndices[] = { s_GhostLvDef, s_GhostEndCountDef, DUAL_FAME_FIELD, HONOR_QUEST_NO, HONOR_MAX_QUEST_NO };
                if (index < 5) var[cn][scriptIndices[index]] = level;
                else if (index == 5) ch->faith = static_cast<short>(level);
                else ch->god_cast_level = static_cast<short>(level);
                if (index >= 5) SendLoginServerForCharUpdate(static_cast<short>(cn));
                reply.success = 1;
            }
            if (reply.success) { ResetAbility(ch); changed = true; }
            else if (!reply.message[0]) strcpy_s(reply.message, "Valor invalido. Taticas: 1 ate o limite do servidor; profissoes: 0-100; skills: 0-1; combate: 0-3.");
        }
        break;
    case LA_ATTRIBUTES:
        if (!ValidAttributes(request.state))
            strcpy_s(reply.message, "Atributos e pontos: 0 a 32767. Nivel fora da faixa do servidor.");
        else
        {
            AttributeUndo previous;
            previous.character = ch->Name;
            ReadState(ch, previous.state);
            undoAttributes[cn] = previous;
            ApplyAttributes(ch, request.state);
            reply.success = 1;
            changed = true;
        }
        break;
    case LA_UNDO_ATTRIBUTES:
        if (undoAttributes.count(cn) && undoAttributes[cn].character == ch->Name)
        {
            ApplyAttributes(ch, undoAttributes[cn].state);
            undoAttributes.erase(cn);
            reply.success = 1;
            changed = true;
        }
        else strcpy_s(reply.message, "Nenhuma edicao de atributos para desfazer nesta sessao.");
        break;
    case LA_GOLD:
        if (request.state.gold > 2000000000U)
            strcpy_s(reply.message, "Ouro deve estar entre 0 e 2000000000.");
        else
        {
            const DWORD current = GetMoneyByItem(ch);
            ch->Money = current;
            if (request.state.gold > current)
            {
                const int kind = LocalMgr.IsChangeMoney() ? IK_NEW_MONEY : IK_MONEY;
                const ItemAttr* inventory = &ch->inv[0][0][0];
                int coinSlots = 0;
                for (int i = 0; i < 72; ++i)
                {
                    CItem* item = ItemUnit(inventory[i]);
                    if (item && inventory[i].item_no && item->GetItemKind() == kind) ++coinSlots;
                }
                const DWORD slotsNeeded = (request.state.gold + MAX_MONEY - 1) / MAX_MONEY;
                if (slotsNeeded > static_cast<DWORD>(coinSlots + EmptySlots(ch)))
                    strcpy_s(reply.message, "Espacos insuficientes para as pilhas de moedas desse saldo.");
                else reply.success = AddMoney(request.state.gold - current, ch) > 0;
            }
            else reply.success = SubtractMoney(current - request.state.gold, ch) == 1;
            ch->Money = GetMoneyByItem(ch);
            ch->SendCharInfoBasic(MONEY, ch->Money);
            changed = reply.success != 0;
        }
        break;
    case LA_BANK:
        if (request.state.bank <= MAX_BANK_MONEY)
        {
            ch->SetBankMoney(request.state.bank, BMCT_RECV_CHAR_INFO_BASIC);
            ch->SendCharInfoBasic(BANKMONEY, ch->GetBankMoney());
            reply.success = 1;
            changed = true;
        }
        else strcpy_s(reply.message, "Saldo do banco acima do limite suportado.");
        break;
    case LA_EMBLEMS:
        if (request.state.emblems <= 1000000000U)
        {
            reply.success = SetEmblems(ch, request.state.emblems);
            changed = reply.success != 0;
            if (!reply.success) strcpy_s(reply.message, "Libere um espaco no inventario para RE.");
        }
        else strcpy_s(reply.message, "RE deve estar entre 0 e 1000000000.");
        break;
    case LA_PREVIEW:
        {
            drafts.erase(cn);
            ItemDraft draft;
            if (PrepareItem(request, draft, reply.message, sizeof(reply.message)))
            {
                draft.character = ch->Name;
                draft.token = ++nextPreviewToken;
                if (!draft.token) draft.token = ++nextPreviewToken;
                drafts[cn] = draft;
                reply.previewToken = draft.token;
                reply.success = 1;
            }
        }
        break;
    case LA_CREATE:
        {
            ItemDraft draft;
            if (request.previewToken)
            {
                if (!drafts.count(cn) || drafts[cn].character != ch->Name ||
                    drafts[cn].token != request.previewToken)
                {
                    strcpy_s(reply.message, "A previa expirou. Gere outra ou crie sem previa.");
                    break;
                }
                draft = drafts[cn];
            }
            else if (!PrepareItem(request, draft, reply.message, sizeof(reply.message)))
                break;
            if (EmptySlots(ch) < draft.copies)
                strcpy_s(reply.message, "Espacos insuficientes no inventario. Nenhum item foi criado.");
            else
            {
                // The map thread owns inventory mutations. All slots were checked before insertion.
                for (int i = 0; i < draft.copies; ++i)
                {
                    ItemAttr item = draft.item;
                    InsertItem(ch, item);
                }
                drafts.erase(cn);
                reply.success = 1;
                changed = true;
                strcpy_s(reply.message, "Item adicionado ao inventario.");
            }
        }
        break;
    default:
        strcpy_s(reply.message, "Operacao desconhecida.");
        break;
    }
    if (authorized)
    {
        ReadState(ch, reply.state);
        reply.canUndo = undoAttributes.count(cn) && undoAttributes[cn].character == ch->Name;
    }
    if (changed)
    {
        updateCharacterStatusToLoginServer(connections, cn);
        if (request.operation == LA_DUAL || request.operation == LA_SYMBOL || request.operation == LA_PROGRESSION || request.operation == LA_PRESET || request.operation == LA_UNDO_TACTIC)
            updateBinaryDataToLoaginServer(connections, cn);
        if (request.operation == LA_CREATE || request.operation == LA_GOLD || request.operation == LA_EMBLEMS)
        {
            t_packet inventory = {};
            inventory.h.header.type = CMD_UPDATE_INV_DATA;
            inventory.h.header.size = sizeof(t_server_update_inv_data);
            strcpy_s(inventory.u.server_update_inv_data.name, connections[cn].name);
            strcpy_s(inventory.u.server_update_inv_data.id, connections[cn].id);
            memcpy(inventory.u.server_update_inv_data.inv, ch->inv, SIZE_OF_INV);
            inventory.u.server_update_inv_data.check_crc = AddCRC(inventory.u.data, inventory.h.header.size - 4, 10);
            QueuePacket(connections, DB_DEMON, &inventory, 1);
        }
        MyLog(LOG_NORMAL, "LOCAL GM account=%s character=%s operation=%u request=%u", account, ch->Name, request.operation, request.requestId);
        if (!reply.message[0]) strcpy_s(reply.message, "Aplicado no servidor. Salvamento enviado ao DBDemon.");
    }
    if (authorized && (!eventOperator || eventOperation) && (changed || request.operation == LA_SUMMON || request.operation == LA_SUMMON_REMOVE ||
        request.operation == LA_ANNOUNCE || request.operation == LA_ANNOUNCE_ALL || request.operation == LA_RECOVER || request.operation == LA_REVIVE || request.operation == LA_TRAINING))
        Audit(cn, request, reply);
    if (!reply.message[0]) strcpy_s(reply.message, "Operacao nao aplicada.");
    const bool verifyTactics = changed && (request.operation == LA_PRESET || request.operation == LA_UNDO_TACTIC ||
        (request.operation == LA_PROGRESSION && request.progressionKind == 0));
    if (verifyTactics)
    {
        LocalAdminTacticVerification verification = {};
        verification.connection = cn;
        verification.requestId = reply.requestId;
        strcpy_s(verification.character, reply.state.character);
        memcpy(verification.experience, reply.state.tacticExp, sizeof(verification.experience));
        memcpy(verification.step, reply.state.tacticStep, sizeof(verification.step));
        const TacticSave pending = { connections[cn].dwIDForAgent, reply };
        tacticSaves[cn] = pending;
        t_packet packet = {};
        packet.h.header.type = CMD_LOCAL_ADMIN_VERIFY_TACTICS;
        packet.h.header.size = sizeof(verification);
        memcpy(&packet.u, &verification, sizeof(verification));
        QueuePacket(connections, DB_DEMON, &packet, 1);
        return;
    }
    SendAdminReply(cn, reply);
    if (authorized && request.operation == LA_READ && reply.success)
        SendEffectCatalog(cn, request.requestId);
}


void HandleLocalAdminVerification(const void* payload, int length)
{
    if (length != sizeof(LocalAdminTacticVerification)) return;
    LocalAdminTacticVerification verification;
    memcpy(&verification, payload, sizeof(verification));
    auto pending = tacticSaves.find(verification.connection);
    if (pending == tacticSaves.end() || pending->second.reply.requestId != verification.requestId ||
        memcmp(pending->second.reply.state.character, verification.character, sizeof(verification.character))) return;

    const DWORD playerId = pending->second.playerId;
    LocalAdminReply reply = pending->second.reply;
    tacticSaves.erase(pending);
    CHARLIST* ch = CheckServerId(verification.connection);
    if (!ch || connections[verification.connection].state != CONNECT_JOIN ||
        connections[verification.connection].dwIDForAgent != playerId || strcmp(ch->Name, reply.state.character)) return;
    ReadState(ch, reply.state);
    if (verification.verified == 1)
        strcpy_s(reply.message, "Aplicado. XP e etapas das taticas confirmados no banco.");
    else
        strcpy_s(reply.message, "Aplicado em memoria. O banco nao confirmou XP e etapas das taticas.");
    SendAdminReply(verification.connection, reply);
}

void HandleLocalAdminMapNotice(const void* payload, int length)
{
    char message[120];
    if (length != sizeof(message)) return;
    memcpy(message, payload, sizeof(message));
    message[sizeof(message) - 1] = 0;
    for (int player = DRAGON_CONNECTIONS_START; player < DRAGON_MAX_CONNECTIONS; ++player)
        if (connections[player].state == CONNECT_JOIN && CheckServerId(player))
            SendBBS2(message, static_cast<short>(player));
}

void HandleLocalAdminMapRoster(const void* payload, int length)
{
    if (length != sizeof(LocalAdminMapRoster)) return;
    LocalAdminMapRoster roster;
    memcpy(&roster, payload, sizeof(roster));
    if (!roster.ticket || !roster.originPort) return;
    if (!roster.response)
    {
        LP_SERVER_DATA source = g_pServerTable->GetConnectedServerData(roster.originPort);
        if (!source || source->dwServerType != SERVER_TYPE_MAP) return;
        LocalAdminReply reply = {};
        AppendMapRoster(reply);
        strncpy_s(roster.details, reply.details, _TRUNCATE);
        roster.response = 1;
        SendRoster(roster.originPort, roster);
        return;
    }
    roster.details[sizeof(roster.details) - 1] = 0;
    for (const auto& entry : rosterRequests)
    {
        const int player = entry.first;
        const RosterRequest& query = entry.second;
        if (query.ticket != roster.ticket || GetTickCount() - query.started > 10000 ||
            connections[player].dwIDForAgent != query.playerId || connections[player].state != CONNECT_JOIN)
            continue;
        LocalAdminReply reply = {};
        reply.version = LOCAL_ADMIN_VERSION;
        reply.operation = LA_SESSION_ALL;
        reply.requestId = query.requestId;
        reply.previewToken = 1;
        strcpy_s(reply.details, roster.details);
        SendAdminReply(player, reply);
        return;
    }
}
