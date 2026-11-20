"""The build must retain the legacy event's local-time and inclusive interval rules."""
import subprocess
import unittest
from native_test_support import compile_native, function_body


class EventTimeTests(unittest.TestCase):
    def test_event_schedule_without_mfc(self):
        methods = function_body('Mapserver/HigherLayers/eventmgr.cpp', 'int CEventTime::SetTime(')
        methods += '\n' + function_body('Mapserver/HigherLayers/eventmgr.cpp', 'int CEventTime::CheckActive()')
        # CTime is deliberately unavailable: events must compile with the C runtime.
        source = r'''
#include <ctime>
#include <cstdlib>
struct TIMESTAMP_STRUCT { int year,month,day,hour,minute,second; };
struct CEventTime {
  int m_nEventActive; std::time_t m_timeStart,m_timeEnd;
  void Remove() { m_timeStart = m_timeEnd = 0; }
  int SetTime(TIMESTAMP_STRUCT*,TIMESTAMP_STRUCT*); int CheckActive();
};
'''+methods+r'''
int main() {
  _putenv_s("TZ", "UTC"); _tzset();
  CEventTime event;
  TIMESTAMP_STRUCT start = {2026,1,1,0,0,0}, end = {2026,1,2,0,0,0};
  if (!event.SetTime(&start,&end)) return 1;
  if (event.m_timeStart != 1767225600LL || event.m_timeEnd != 1767312000LL) return 2;
  const auto now = std::time(NULL);
  event.m_timeStart = now - 60; event.m_timeEnd = now + 60;
  if (!event.CheckActive()) return 3;
  event.m_timeStart = now + 60;
  if (event.CheckActive()) return 4;
  event.m_timeStart = now - 120; event.m_timeEnd = now - 60;
  if (event.CheckActive()) return 5;
  return 0;
}
'''
        exe = compile_native(source, 'event_time')
        result = subprocess.run([str(exe)], capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0)
