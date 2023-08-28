import 'dart:io';

import 'package:coast_audio_native_codec_example/player_screen.dart';
import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:flutter_coast_audio_miniaudio/flutter_coast_audio_miniaudio.dart';

void main() {
  MabLibrary.initialize();
  MabDeviceContext.enableSharedInstance(backends: [MabBackend.coreAudio, MabBackend.aaudio]);
  runApp(const App());
}

class App extends StatefulWidget {
  const App({super.key});

  @override
  State<App> createState() => _AppState();
}

class _AppState extends State<App> {
  AudioInputDataSource? _dataSource;
  SyncDisposable? _dataSourceDisposable;

  @override
  void dispose() {
    _dataSourceDisposable?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      home: Scaffold(
        appBar: AppBar(
          title: const Text('coast_audio_native_codec'),
        ),
        body: Column(
          children: [
            Padding(
              padding: const EdgeInsets.all(16),
              child: Row(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  TextButton(
                    onPressed: () async {
                      FilePickerResult? result = await FilePicker.platform.pickFiles();

                      if (result != null) {
                        _openFile(File(result.files.single.path!));
                      }
                    },
                    child: const Text('Open File'),
                  ),
                ],
              ),
            ),
            const Divider(),
            if (_dataSource != null)
              Expanded(
                child: PlayerScreen(
                  key: ValueKey(_dataSource),
                  dataSource: _dataSource!,
                ),
              ),
          ],
        ),
      ),
    );
  }

  void _openFile(File file) {
    setState(() {
      final dataSource = AudioFileDataSource(file: file, mode: FileMode.read);
      _dataSource = dataSource;
      _dataSourceDisposable = dataSource;
    });
  }
}
