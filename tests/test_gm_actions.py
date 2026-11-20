"""Execute the actual server case handlers with inventory/item boundaries stubbed."""
import re
import subprocess
import unittest

from native_test_support import ROOT, compile_native

HARNESS = r'''
#include <cstring>
#include <map>
#include <string>
#include <iostream>
#include <stdexcept>
#include "Shared/LocalAdminProtocol.h"
enum { VIEWTYPE_GHOST_ = 3 };
static_assert(LA_TELEPORT == 13 && LA_AUDIT == 29 && LA_REVIVE == 30, "Existing GM operations must keep their IDs");
struct ItemAttr { int item_no; };
struct ItemDraft { std::string character; unsigned token; ItemAttr item; int copies; };
struct Character { std::string Name = "tester"; int viewtype = VIEWTYPE_GHOST_; bool dead = true; bool IsDead() const { return dead; } };
struct Request { unsigned operation = LA_CREATE, previewToken = 0; int itemNumber = 10057, quantity = 1; };
struct Reply { int success = 0; char message[256] = {}; };
std::map<int, ItemDraft> drafts;
int freeSlots = 10, inserted = 0, prepared = 0, reviveCalls = 0, reviveMode = -1;
bool PrepareItem(const Request& request, ItemDraft& draft, char*, size_t) {
    ++prepared;
    if (request.itemNumber < 1 || request.quantity < 1) return false;
    draft.item.item_no = request.itemNumber;
    draft.copies = request.quantity;
    return true;
}
int EmptySlots(Character*) { return freeSlots; }
void InsertItem(Character*, ItemAttr item) { if (item.item_no) ++inserted; }
struct Skills { void CharacterToAlive(Character*, int mode) { ++reviveCalls; reviveMode = mode; } } SkillMgr;
Reply Handle(Request request, Character* ch) {
    int cn = 1; bool changed = false;
    Reply reply;
    switch (request.operation) {
'''
MAIN = r'''
    }
    return reply;
}
void check(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
int main(int argc, char** argv) {
 try {
    std::string which = argv[1]; Character character; Request request;
    if (which == "preview" || which == "stale" || which == "wrong-character") {
        drafts[1] = {which == "wrong-character" ? "other" : "tester", 77, {10057}, 2};
        request.previewToken = which == "stale" ? 78 : 77;
        Reply r = Handle(request, &character);
        if (which == "preview") check(r.success && inserted == 2 && prepared == 0, "preview must preserve its prepared item");
        else check(!r.success && inserted == 0 && prepared == 0, "invalid preview token must never fall back to direct creation");
    } else if (which == "alive" || which == "ghost") {
        request.operation = LA_REVIVE;
        if (which == "alive") { character.viewtype = 0; character.dead = false; }
        Reply r = Handle(request, &character);
        if (which == "ghost") check(r.success && reviveCalls == 1 && reviveMode == 1, "ghost must use the full revival-item effect");
        else check(!r.success && reviveCalls == 0, "living character must not receive a free heal");
    } else {
        if (which == "invalid") request.itemNumber = 0;
        if (which == "full") freeSlots = 0;
        Reply r = Handle(request, &character);
        if (which == "direct") check(r.success && inserted == 1 && prepared == 1, "valid creation must not require preview");
        else check(!r.success && inserted == 0 && prepared == 1, "direct creation must validate before inventory mutation");
    }
    return 0;
 } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
'''


class GmActionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        text = (ROOT / 'Mapserver/HigherLayers/LocalAdminServer.cpp').read_text(encoding='utf-8')
        cases = []
        for operation in ('LA_CREATE', 'LA_REVIVE'):
            found = re.search(r'^    case ' + operation + r':.*?(?=^    (?:case |default:))', text, re.M | re.S)
            if found: cases.append(found.group())
        cls.exe = compile_native(HARNESS + '\n'.join(cases) + MAIN, 'gm_actions')

    def run_case(self, name):
        result = subprocess.run([str(self.exe), name], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_create_without_preview(self): self.run_case('direct')
    def test_direct_create_still_validates_item(self): self.run_case('invalid')
    def test_direct_create_checks_space_first(self): self.run_case('full')
    def test_preview_token_uses_exact_draft(self): self.run_case('preview')
    def test_stale_preview_cannot_create(self): self.run_case('stale')
    def test_preview_is_bound_to_character(self): self.run_case('wrong-character')
    def test_ghost_uses_revival_item_effect(self): self.run_case('ghost')
    def test_living_character_cannot_heal(self): self.run_case('alive')
