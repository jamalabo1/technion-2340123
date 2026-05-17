//
// Created by Jamal Abo Mokh on 17/05/2026.
//

#ifndef CHILD_TRACKER_H
#define CHILD_TRACKER_H

class ChildTracker {
  public:
    // currently a simple flag, maybe grow later.
    bool isChild;

    ChildTracker();

    int fork();
};

#endif //CHILD_TRACKER_H
