import 'dart:async';
import 'dart:math';

import 'package:coast_audio_native_codec/coast_audio_native_codec.dart';
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter_coast_audio_miniaudio/flutter_coast_audio_miniaudio.dart';

class PlayerScreen extends StatefulWidget {
  const PlayerScreen({
    super.key,
    required this.dataSource,
  });
  final AudioInputDataSource dataSource;

  @override
  State<PlayerScreen> createState() => _PlayerScreenState();
}

class _PlayerScreenState extends State<PlayerScreen> {
  NativeAudioFormat? nativeFormat;
  NativeAudioDecoder? decoder;
  MabAudioPlayer? player;

  @override
  void initState() {
    _initPlayer();
    Timer.periodic(const Duration(milliseconds: 100), (timer) {
      if (!mounted) {
        timer.cancel();
        return;
      }

      setState(() {});
    });
    super.initState();
  }

  Future<void> _initPlayer() async {
    try {
      final decoder = NativeAudioDecoder(dataSource: widget.dataSource);
      final player = MabAudioPlayer(format: decoder.outputFormat, isLoop: true);
      await player.open(decoder);

      setState(() {
        nativeFormat = decoder.nativeFormat;
        this.decoder = decoder;
        this.player = player;
      });
    } on Exception catch (e) {
      if (!mounted) {
        return;
      }

      SchedulerBinding.instance.addPostFrameCallback((timeStamp) {
        showDialog(
          context: context,
          builder: (context) {
            return SimpleDialog(
              title: const Text('Exception Occurred'),
              contentPadding: const EdgeInsets.all(24),
              children: [
                Text(e.toString()),
                const SizedBox(height: 16),
                Align(
                  alignment: Alignment.centerRight,
                  child: ElevatedButton(
                    onPressed: () => Navigator.of(context).pop(),
                    child: const Text('OK'),
                  ),
                ),
              ],
            );
          },
        );
      });
    }
  }

  @override
  void dispose() {
    player?.dispose();
    decoder?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final isPlaying = player?.state == MabAudioPlayerState.playing;

    return Scaffold(
      body: ListView(
        children: [
          ListTile(
            title: const Text('SampleRate(Hz)'),
            trailing: Text(nativeFormat?.sampleRate.toString() ?? ''),
          ),
          ListTile(
            title: const Text('Channels'),
            trailing: Text(nativeFormat?.channels.toString() ?? ''),
          ),
          ListTile(
            title: const Text('SampleFormat'),
            trailing: Text(nativeFormat?.sampleFormat.toString() ?? ''),
          ),
          if (decoder != null && nativeFormat != null)
            ListTile(
              title: const Text('Duration'),
              trailing: Text(
                  '${AudioTime.fromFrames(frames: decoder!.cursorInFrames, format: decoder!.outputFormat).formatMMSS()}/${AudioTime.fromFrames(frames: nativeFormat!.length, format: decoder!.outputFormat).formatMMSS()}'),
            ),
          if (decoder != null && nativeFormat != null)
            Slider(
              value: AudioTime.fromFrames(frames: decoder!.cursorInFrames, format: decoder!.outputFormat).seconds,
              max: AudioTime.fromFrames(frames: max(decoder!.lengthInFrames, decoder!.cursorInFrames), format: decoder!.outputFormat).seconds,
              onChanged: (value) {
                decoder!.cursorInFrames = AudioTime(value).computeFrames(decoder!.outputFormat);
              },
            ),
          const Divider(),
          ListTile(
            title: const Text('AudioFormatID (Apple)'),
            trailing: Text(nativeFormat?.apple?.foramtId.text ?? 'NULL'),
          ),
          const Divider(),
        ],
      ),
      floatingActionButton: FloatingActionButton(
        child: Icon(isPlaying ? Icons.pause : Icons.play_arrow),
        onPressed: () {
          if (isPlaying) {
            player?.pause();
          } else {
            player?.play();
          }
        },
      ),
    );
  }
}
