from pathlib import Path
r=Path(__file__).resolve().parents[2]
j=(r/'android/app/src/main/java/com/ylports/lcsrecomp/MainActivity.java').read_text()
c=(r/'android/app/src/main/cpp/android_bridge.cpp').read_text()
assert 'native boolean nativeCaptureFrame(String path)' in j
assert 'native int nativeCaptureStatus()' in j
assert 'Java_com_ylports_lcsrecomp_MainActivity_nativeCaptureFrame' in c
assert 'Java_com_ylports_lcsrecomp_MainActivity_nativeCaptureStatus' in c
assert 'Intent.ACTION_CREATE_DOCUMENT' in j and 'REQUEST_CAPTURE = 1003' in j
assert 'new AlertDialog.Builder' in j and 'private final Runnable capturePoll' in j
assert 'debugHandler.removeCallbacks(capturePoll)' in j
assert '"LCS-Capture-Export"' in j
assert 'render_capture_finish();' in c
print('PASS: capture JNI/UI/export/lifecycle source contract; Android APK compilation is a separate check')
