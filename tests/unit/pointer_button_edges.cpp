#include "core/input/input_state.h"

#include <cassert>

int main() {
    int firstToken = 0;
    int secondToken = 0;
    const core::window::Handle first = &firstToken;
    const core::window::Handle second = &secondToken;

    core::queuePointerButton(first, 12.0, 24.0, 0, true);
    core::queuePointerButton(first, 12.0, 24.0, 0, false);

    const core::PointerButtonEdges firstEdges = core::consumePointerButtonEdges(first);
    assert(firstEdges.pressed);
    assert(firstEdges.released);
    assert(!firstEdges.rightPressed);
    assert(!firstEdges.rightReleased);
    assert(!core::detail::queuedPointerButtonDown(first, 0));

    const core::PointerButtonEdges consumed = core::consumePointerButtonEdges(first);
    assert(!consumed.pressed);
    assert(!consumed.released);

    core::queuePointerButton(first, 18.0, 28.0, 0, true);
    core::queuePointerButton(first, 78.0, 88.0, 0, false);
    const auto orderedTransitions = core::consumePointerButtonTransitions(first);
    assert(orderedTransitions.size() == 2);
    assert(orderedTransitions[0].down && orderedTransitions[0].x == 18.0 &&
           orderedTransitions[0].y == 28.0);
    assert(!orderedTransitions[1].down && orderedTransitions[1].x == 78.0 &&
           orderedTransitions[1].y == 88.0);

    core::queuePointerButton(second, 30.0, 40.0, 1, true);
    const core::PointerButtonEdges isolatedFirst = core::consumePointerButtonEdges(first);
    const core::PointerButtonEdges secondEdges = core::consumePointerButtonEdges(second);
    assert(!isolatedFirst.rightPressed);
    assert(secondEdges.rightPressed);
    assert(!secondEdges.rightReleased);
    assert(core::detail::queuedPointerButtonDown(second, 1));

    core::queuePointerButton(second, 32.0, 42.0, 1, false);
    const core::PointerButtonEdges rightRelease = core::consumePointerButtonEdges(second);
    assert(rightRelease.rightReleased);
    assert(!core::detail::queuedPointerButtonDown(second, 1));

    const core::Rect bounds{0.0f, 0.0f, 100.0f, 100.0f};
    core::InteractionState click;
    core::PointerEvent sameFrameClick;
    sameFrameClick.x = 20.0;
    sameFrameClick.y = 20.0;
    sameFrameClick.pressedThisFrame = true;
    sameFrameClick.releasedThisFrame = true;
    click.update(bounds, sameFrameClick, true);
    assert(click.clicked);
    assert(click.released);
    assert(!click.pressed);
    assert(!click.active);

    core::InteractionState movedAfterClick;
    core::PointerEvent movedPress;
    movedPress.x = 20.0;
    movedPress.y = 20.0;
    movedPress.down = true;
    movedPress.pressedThisFrame = true;
    movedAfterClick.update(bounds, movedPress, true);
    core::PointerEvent movedRelease = movedPress;
    movedRelease.down = false;
    movedRelease.pressedThisFrame = false;
    movedRelease.releasedThisFrame = true;
    movedAfterClick.update(bounds, movedRelease, true);
    assert(movedAfterClick.clicked);
    core::PointerEvent finalOutside;
    finalOutside.x = 140.0;
    finalOutside.y = 140.0;
    movedAfterClick.update(bounds, finalOutside, false);
    assert(!movedAfterClick.hover);

    core::InteractionState outsideClick;
    core::PointerEvent outsidePress;
    outsidePress.x = 140.0;
    outsidePress.y = 140.0;
    outsidePress.down = true;
    outsidePress.pressedThisFrame = true;
    outsideClick.update(bounds, outsidePress, false);
    core::PointerEvent outsideRelease = outsidePress;
    outsideRelease.down = false;
    outsideRelease.pressedThisFrame = false;
    outsideRelease.releasedThisFrame = true;
    outsideClick.update(bounds, outsideRelease, false);
    assert(!outsideClick.clicked);
    core::PointerEvent finalInside;
    finalInside.x = 20.0;
    finalInside.y = 20.0;
    outsideClick.update(bounds, finalInside, true);
    assert(outsideClick.hover);

    core::InteractionState releaseOutside;
    releaseOutside.update(bounds, movedPress, true);
    core::PointerEvent outsideReleaseAfterPress = finalOutside;
    outsideReleaseAfterPress.releasedThisFrame = true;
    releaseOutside.update(bounds, outsideReleaseAfterPress, true);
    assert(releaseOutside.released);
    assert(!releaseOutside.clicked);

    core::InteractionState drag;
    core::PointerEvent press;
    press.x = 20.0;
    press.y = 20.0;
    press.down = true;
    press.pressedThisFrame = true;
    drag.update(bounds, press, true);
    assert(drag.pressStarted);
    assert(drag.pressed);

    core::PointerEvent move;
    move.x = 30.0;
    move.y = 35.0;
    move.down = true;
    drag.update(bounds, move, true);
    assert(drag.drag);
    assert(drag.pressed);

    core::PointerEvent release;
    release.x = 30.0;
    release.y = 35.0;
    release.releasedThisFrame = true;
    drag.update(bounds, release, true);
    assert(drag.released);
    assert(!drag.drag);
    assert(!drag.active);
    return 0;
}
