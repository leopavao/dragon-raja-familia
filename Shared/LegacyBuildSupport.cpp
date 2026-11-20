// HSEL's legacy object references this MFC link-selection marker, but uses no
// MFC API. Supplying the marker avoids requiring MFC solely to link the cipher.
extern "C" {
    int _afxForceEXCLUDE = 0;
}
