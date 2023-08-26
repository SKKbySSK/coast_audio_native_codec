import 'dart:io';

import 'package:coast_audio_native_codec/coast_audio_native_codec.dart';
import 'package:flutter/material.dart';
import 'package:flutter_coast_audio_miniaudio/flutter_coast_audio_miniaudio.dart';

void main() {
  MabLibrary.initialize();
  MabDeviceContext.enableSharedInstance(backends: [MabBackend.coreAudio, MabBackend.aaudio]);
  runApp(const MyApp());
}

class MyApp extends StatefulWidget {
  const MyApp({super.key});

  @override
  State<MyApp> createState() => _MyAppState();
}

class _MyAppState extends State<MyApp> {
  final decoder = NativeAudioDecoder(
    dataSource: AudioFileDataSource(
      file: File('/Users/kaisei/Music/03 Cool Kids.m4a'),
      mode: FileMode.read,
    ),
  );
  late final nativeFormat = decoder.nativeFormat;
  late final player = MabAudioPlayer(format: decoder.outputFormat, limitMaxPosition: false);

  @override
  void initState() {
    _initPlayer();
    super.initState();
  }

  Future<void> _initPlayer() async {
    try {
      await player.open(decoder);
    } on Exception catch (e) {
      print(e);
    }
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      home: Scaffold(
        appBar: AppBar(
          title: const Text('Native Packages'),
        ),
        body: SingleChildScrollView(
          child: Container(
            padding: const EdgeInsets.all(10),
            child: Column(
              children: [
                ListTile(
                  title: const Text('SampleRate(Hz)'),
                  trailing: Text(nativeFormat.sampleRate.toString()),
                ),
                ListTile(
                  title: const Text('Channels'),
                  trailing: Text(nativeFormat.channels.toString()),
                ),
                ListTile(
                  title: const Text('SampleFormat'),
                  trailing: Text(nativeFormat.sampleFormat.toString()),
                ),
                ListTile(
                  title: const Text('Apple FormatId'),
                  trailing: Text(nativeFormat.apple?.foramtId.text ?? 'NULL'),
                ),
                ElevatedButton(
                  onPressed: () {
                    player.play();
                  },
                  child: const Text('Play'),
                ),
                ElevatedButton(
                  onPressed: () {
                    player.pause();
                  },
                  child: const Text('Pause'),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}
