import 'package:flutter/material.dart';
import 'package:launcher_control/launcher_control.dart';

import '../../app/app_services.dart';

/// A game's portrait cover, the way Steam shows it (2:3), from the user's own
/// Steam library. Without one it is a tile with the game's icon, same size.
class GameCover extends StatelessWidget {
  const GameCover({required this.game, this.width = 96, super.key});

  final GameInfo game;
  final double width;

  double get height => width * 1.5;

  static IconData iconFor(String id) => switch (id) {
    'gearblocks' => Icons.settings_suggest,
    'beamng' => Icons.directions_car_filled,
    _ => Icons.sports_esports,
  };

  @override
  Widget build(BuildContext context) {
    final covers = AppScope.of(context).covers;
    final appId = game.steamAppId;
    final placeholder = _Placeholder(
      icon: iconFor(game.id),
      width: width,
      height: height,
    );
    if (covers == null || appId == null) {
      return placeholder;
    }
    // The art is decoration: the game's name is right next to it.
    return ExcludeSemantics(
      child: FutureBuilder<ImageProvider?>(
        future: covers.coverFor(appId),
        builder: (context, snapshot) {
          final image = snapshot.data;
          if (image == null) {
            return placeholder;
          }
          return ClipRRect(
            borderRadius: BorderRadius.circular(8),
            child: DecoratedBox(
              decoration: BoxDecoration(
                border: Border.all(
                  color: Theme.of(context).colorScheme.outlineVariant,
                ),
                borderRadius: BorderRadius.circular(8),
              ),
              child: Image(
                image: image,
                width: width,
                height: height,
                fit: BoxFit.cover,
                filterQuality: FilterQuality.medium,
                // the icon tile until the art is decoded, no blank flash
                frameBuilder: (_, child, frame, synchronous) =>
                    frame == null && !synchronous ? placeholder : child,
                errorBuilder: (_, _, _) => placeholder,
              ),
            ),
          );
        },
      ),
    );
  }
}

class _Placeholder extends StatelessWidget {
  const _Placeholder({
    required this.icon,
    required this.width,
    required this.height,
  });

  final IconData icon;
  final double width;
  final double height;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Container(
      width: width,
      height: height,
      decoration: BoxDecoration(
        color: scheme.primaryContainer,
        borderRadius: BorderRadius.circular(8),
      ),
      child: Icon(icon, size: width * 0.45, color: scheme.onPrimaryContainer),
    );
  }
}
