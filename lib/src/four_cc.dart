class FourCC {
  FourCC(this.rawValue)
      : text = String.fromCharCodes([
          rawValue >> 24 & 255,
          rawValue >> 16 & 255,
          rawValue >> 8 & 255,
          rawValue >> 0 & 255,
        ]);

  final int rawValue;
  final String text;
}
