"""Regression: an encrypted TCP batch must survive short writes and backpressure.

Only the socket and dequeue boundaries are substituted. HandleWriting is compiled
from the current game's source, so deleting its retention logic breaks these tests.
"""
import subprocess
import unittest

from native_test_support import compile_native, function_body

HARNESS = r'''
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
const int MAX_PACKET_SIZE = 3000;
const int MAX_CLIENT_QUEUE_OUT_BUFFER = 16584;
const int WSAEWOULDBLOCK = 10035, WSAEINTR = 10004;
struct t_header { short type, size; };
struct t_packet {
    union { t_header header; char data[sizeof(t_header)]; } h;
    union { char data[MAX_PACKET_SIZE]; } u;
    t_packet* next;
};
struct t_connection {
    int socket;
    t_packet* outbuf;
    char sendBuffer[MAX_CLIENT_QUEUE_OUT_BUFFER];
    int sendBufferSize, sendBufferOffset;
};
t_connection connections = {};
int data_size = 0;
unsigned long g_SendBytes = 0;
std::vector<char> received;
std::vector<int> schedule;
unsigned step = 0;
int dequeueCount = 0;
int WSAGetLastError() { return WSAEWOULDBLOCK; }
int send(int, const char* data, int size, int) {
    int count = step < schedule.size() ? schedule[step++] : size;
    if (count < 0) return -1;
    count = (std::min)(count, size);
    received.insert(received.end(), data, data + count);
    return count;
}
int DequeuePacket(t_packet* packet, int) {
    if (!connections.outbuf) return 0;
    *packet = *connections.outbuf;
    data_size = packet->h.header.size;
    auto old = connections.outbuf;
    connections.outbuf = old->next;
    delete old;
    ++dequeueCount;
    return 1;
}
void require(bool pass, const char* message) {
    if (!pass) throw std::runtime_error(message);
}
void enqueue(const std::string& text, std::vector<char>& expected) {
    t_packet* p = new t_packet();
    p->h.header.type = 32690;
    p->h.header.size = static_cast<short>(text.size());
    memcpy(p->u.data, text.data(), text.size());
    auto* slot = &connections.outbuf;
    while (*slot) slot = &(*slot)->next;
    *slot = p;
    short len = static_cast<short>(sizeof(t_header) + text.size());
    expected.insert(expected.end(), reinterpret_cast<char*>(&len), reinterpret_cast<char*>(&len) + 2);
    expected.insert(expected.end(), p->h.data, p->h.data + sizeof(t_header));
    expected.insert(expected.end(), text.begin(), text.end());
}
'''

MAIN = r'''
int main(int argc, char** argv) {
  try {
    std::string which = argv[1];
    connections.socket = 1;
    std::vector<char> expected;
    enqueue(std::string(1400, 'G'), expected);
    if (which == "full") {
      require(HandleWriting(&connections) >= 0, "full send rejected");
    } else if (which == "backpressure") {
      schedule = {13, -1};
      require(HandleWriting(&connections) >= 0, "would-block closed the connection");
      enqueue("later-action", expected);
      for (int n = 0; n < 6 && received.size() < expected.size(); ++n)
        require(HandleWriting(&connections) >= 0, "resume failed");
      require(dequeueCount == 2, "packets must be dequeued/encrypted exactly once");
    } else if (which == "initial-block") {
      schedule = {-1, -1, 9, -1};
      for (int n = 0; n < 6 && received.size() < expected.size(); ++n)
        require(HandleWriting(&connections) >= 0, "temporary blocking is not a disconnect");
      require(dequeueCount == 1, "retry dequeued the encrypted packet again");
    } else if (which == "closed") {
      schedule = {0, -1};
      require(HandleWriting(&connections) < 0, "closed peer must not spin forever");
      std::cout << "closed peer handled\n";
      return 0;
    }
    require(received == expected, "TCP stream lost, duplicated or reordered bytes");
    require(g_SendBytes == received.size(), "byte counter must count successful sends only");
    std::cout << "stream preserved\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
'''


class NetworkStreamTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        body = function_body('dragon2022/network.cpp', 'int HandleWriting( t_connection *c )')
        cls.exe = compile_native(HARNESS + '\n' + body + '\n' + MAIN, 'network_stream')

    def run_case(self, name):
        result = subprocess.run([str(self.exe), name], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_full_batch(self): self.run_case('full')
    def test_short_send_then_backpressure_retains_order(self): self.run_case('backpressure')
    def test_repeated_would_block_keeps_encrypted_batch(self): self.run_case('initial-block')
    def test_closed_peer_is_an_error(self): self.run_case('closed')


if __name__ == '__main__':
    unittest.main()
