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
#include <map>
#include <string>

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
            request.highItem < 0 || request.highItem > 1 ||
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
    if (length != sizeof(LocalAdminRequest)) return;
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
    authorized = authorized && GetPrivateProfileIntA("local_admin", "enabled", 0, ".\\MapServer.ini");
    const bool mutation = request.operation != LA_READ && request.operation != LA_PREVIEW;
    LP_SERVER_DATA database = g_pServerTable->GetOwnDBDemonData();
    bool changed = false;
    if (!authorized)
        strcpy_s(reply.message, "Conta sem acesso ao painel GM local.");
    else if (mutation && (!database || !database->dwConnectionIndex || !CanSaveUserData(ch, 4)))
        strcpy_s(reply.message, "Personagem ou banco ainda indisponivel para salvar. Tente novamente.");
    else switch (request.operation)
    {
    case LA_READ:
        drafts.erase(cn);
        reply.success = 1;
        strcpy_s(reply.message, "Dados recebidos do servidor.");
        break;
    case LA_SUMMON:
        if (request.itemNumber < 0 || request.itemNumber >= Num_Of_NPC_Generation ||
            !NPC_Gen_Ref[request.itemNumber].Name[0] || NPC_Gen_Ref[request.itemNumber].nHpMax <= 0 ||
            NPC_Gen_Ref[request.itemNumber].SprNO <= 0 || request.quantity < 1 || request.quantity > 20)
            strcpy_s(reply.message, "Selecione um monstro valido e quantidade de 1 a 20.");
        else if (summonTimes.count(cn) && GetTickCount() - summonTimes[cn] < 3000)
            strcpy_s(reply.message, "Aguarde 3 segundos entre invocacoes.");
        else {
            summonTimes[cn] = GetTickCount();
            int created = 0, upgraded = 0;
            for (int attempt = 0; attempt < 100 && created < request.quantity; ++attempt) {
                int x = ch->X / TILE_SIZE + (attempt % 9) - 4;
                int y = ch->Y / TILE_SIZE + ((attempt / 9) % 9) - 4;
                if (x < 1 || y < 1 || x >= g_Map.file.wWidth - 1 || y >= g_Map.file.wHeight - 1) continue;
                if (!IsMovable(x, y)) continue;
                const int slot = GetAliveNPCList();
                if (slot < 0) break;
                if (NPC_Create(slot, request.itemNumber, x, y, -1, 0, 9)) {
                    ++created;
                    upgraded += MaximizeSummonDropRarity(&NPCList[slot]);
                }
            }
            reply.success = created > 0;
            sprintf_s(reply.message, "%d de %d invocados. Loot normal da especie; raridade maxima nos equipamentos compativeis (%d convertidos). Sem respawn.", created, request.quantity, upgraded);
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
    case LA_PROGRESSION:
        {
            const int kind = request.progressionKind, index = request.progressionIndex;
            const int level = request.progressionLevel;
            if (kind == 0 && index >= 0 && index < 13 && level >= 0 && level <= MAX_TAC_LEVEL) {
                ch->Skill[88 + index] = static_cast<BYTE>(level);
                ch->SetTacticsStep(index, NPC_Lev_Ref[level].nStep);
                ch->tac_skillEXP[index] = NPC_Lev_Ref[level].nMinExp;
                ch->SendCharInfoTactic(index);
                reply.success = 1;
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
            else strcpy_s(reply.message, "Valor invalido. Taticas: limite do servidor; profissoes: 0-100; skills: 0-1; combate: 0-3.");
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
        if (!drafts.count(cn) || drafts[cn].character != ch->Name ||
            drafts[cn].token != request.previewToken || !request.previewToken)
            strcpy_s(reply.message, "Gere uma nova previa antes de adicionar o item.");
        else
        {
            const ItemDraft draft = drafts[cn];
            if (EmptySlots(ch) < draft.copies)
                strcpy_s(reply.message, "Espacos insuficientes no inventario. A previa foi mantida.");
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
        if (request.operation == LA_DUAL || request.operation == LA_SYMBOL || request.operation == LA_PROGRESSION)
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
    if (!reply.message[0]) strcpy_s(reply.message, "Operacao nao aplicada.");
    t_packet packet = {};
    packet.h.header.type = CMD_LOCAL_ADMIN;
    packet.h.header.size = sizeof(reply);
    memcpy(&packet.u, &reply, sizeof(reply));
    QueuePacket(connections, cn, &packet, 1);
    if (authorized && request.operation == LA_READ && reply.success)
        SendEffectCatalog(cn, request.requestId);
}

