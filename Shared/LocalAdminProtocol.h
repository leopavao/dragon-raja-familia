#pragma once
#include <stdint.h>

const int CMD_LOCAL_ADMIN = 32690;
const int CMD_LOCAL_ADMIN_CATALOG = 32691;
const int CMD_LOCAL_ADMIN_VERIFY_TACTICS = 32692;
const int CMD_LOCAL_ADMIN_MAP_NOTICE = 32693;
const int CMD_LOCAL_ADMIN_MAP_ROSTER = 32694;
const uint32_t LOCAL_ADMIN_VERSION = 5;
const int LOCAL_ADMIN_ATTRIBUTE_COUNT = 11;

enum LocalAdminOperation
{
    LA_READ = 1,
    LA_ATTRIBUTES,
    LA_GOLD,
    LA_BANK,
    LA_EMBLEMS,
    LA_PREVIEW,
    LA_CREATE,
    LA_UNDO_ATTRIBUTES,
    LA_DUAL,
    LA_SYMBOL,
    LA_PROGRESSION,
    LA_SUMMON,
    LA_TELEPORT,
    LA_SESSION,
    LA_ANNOUNCE,
    LA_GATHER,
    LA_SUMMON_LIST,
    LA_SUMMON_REMOVE,
    LA_PRESET,
    LA_MAP_SEARCH,
    LA_UNDO_TACTIC,
    LA_INSPECT,
    LA_LEGEND_CATALOG,
    LA_RECOVER,
    LA_TRAINING,
    LA_TRAINING_STATS,
    LA_SESSION_ALL,
    LA_ANNOUNCE_ALL,
    LA_AUDIT
};

#pragma pack(push, 1)
struct LocalAdminState
{
    char character[20];
    int32_t attributes[LOCAL_ADMIN_ATTRIBUTE_COUNT];
    int32_t level;
    uint32_t experience;
    int32_t points;
    uint32_t gold;
    uint32_t bank;
    uint32_t emblems;
    int32_t baseClass, dualClass, dualStep, symbolGrade, symbolNumber;
    int32_t fame, ladder, combatPoints;
    uint8_t skills[200];
    uint8_t wizard[200], priest[200];
    uint32_t tacticExp[13];
    int8_t tacticStep[13];
    char map[20];
    int32_t mapX, mapY;
    uint32_t workExp[45], workRare[45];
    int32_t extraProgress[7];
};

struct LocalAdminRequest
{
    uint32_t version;
    uint32_t operation;
    uint32_t requestId;
    uint32_t previewToken;
    LocalAdminState state;
    int32_t itemNumber;
    int32_t quantity;
    int32_t durability;
    int32_t grade;
    int32_t effects[3];
    int32_t dynamicEffect;
    int32_t highItem;
    int32_t progressionKind, progressionIndex, progressionLevel;
    uint32_t progressionExp, progressionRare;
    char announcement[120];
    char target[20];
    int32_t legendVariant;
    uint32_t summonGroup;
};

struct LocalAdminReply
{
    uint32_t version;
    uint32_t operation;
    uint32_t requestId;
    uint32_t success;
    uint32_t previewToken;
    uint32_t canUndo;
    LocalAdminState state;
    char message[256];
    char details[1450];
    uint32_t summonGroup;
};

struct LocalAdminMapRoster
{
    uint32_t ticket;
    uint16_t originPort;
    uint8_t response;
    char details[1024];
};

struct LocalAdminTacticVerification
{
    uint32_t requestId;
    int32_t connection;
    uint8_t verified;
    char character[20];
    uint32_t experience[13];
    int8_t step[13];
};

struct LocalAdminEffect
{
    int32_t id;
    int32_t dynamicEffect;
    char name[100];
};

struct LocalAdminCatalog
{
    uint32_t version;
    uint32_t requestId;
    uint32_t first;
    uint32_t last;
    uint32_t count;
    LocalAdminEffect effects[24];
};
#pragma pack(pop)
