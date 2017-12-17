boolean verify_beat(const char beat[]) {
  unsigned long  b = strtoul(beat, NULL, 10);
  if (!b) {
    Log.print("Unparsable beat - ignoring.");
    return false;
  };

  unsigned long delta = llabs((long long) b - (long long)beatCounter);

  // otherwise - only accept things in a 4 minute window either side.
  //
  if ((beatCounter < 3600) || (delta < 120)) {
    beatCounter = b;
    if (delta > 10) {
      Log.print("Adjusting beat by "); Log.print(delta); Log.println(" seconds.");
    } else if (delta) {
      Debug.print("Adjusting beat by "); Debug.print(delta); Debug.println(" seconds.");
    }
  } else {
    Log.print("Good message -- but beats ignored as they are too far off ("); Log.print(delta); Log.println(" seconds).");
    return true;
  };
  return false;
}

