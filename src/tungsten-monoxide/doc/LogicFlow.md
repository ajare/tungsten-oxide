# Logic Flow

## States

The game starts off in the first State registered.  This is done in the application, typically in ``DLL.cpp``.  For TungstenMonoxide,
The game starts off in the **Controller** State.

### StateController

This has hardcoded logic to act as an intermediary between different states. This State defines in its constructor that the next state
should be the **Load** State, and its ``update()`` method immediately moves to that State.

### StateLoad

This state loads all resources in the global namespace, by default in a threaded fashion.  It can be set to unthreaded by setting an
argument in the Launcher config file:

```
<Arguments>
	<Argument name="ThreadedLoading">false</Argument>
</Arguments>
```

Upon loading being finished, control returns to the **Controller** State.

### StateController

The function ``getNextStateName()`` defines the following flow from here on:

- **Load** -> **MapLoad**
- **MapLoad** -> **Play**
- **Play** -> **MapTransition** or **MapUnload**, depending on the current ``mMapCount`` value.
- **MapUnload** -> **Unload**
- **Unload** is the final State, and exits.

It also modifies the **StateTransitionData** which is used to communicate between States.  This is
done in ``StateControllerTungstenMonoxide::updateTransitionData()``.

### StateMapLoad

This State loads all resources related to the given **Map**.  The name of the Map is set in ``StateControllerTungstenMonoxide::updateTransitionData()``,
and corresponds to a namespace in the resource file.

After loading, a **WorldRenderer** instance is created in post, and passed in **TransitionData** to the next State.

### StatePlay

This is the main game loop.

The entry point is ``StatePlayTungstenMonoxide::setup()``, which:

- Creates Entity Management, registering a factory for triangle meshes.
- Sets up initial Entities, ie the Player.
- Sets up the Map Renderer (ie the **WorldRenderer**).
- Sets up specific inputs for movement.
- Sets function to handle collisions between Entities and the World.

The update loop does the following:

- Updates the World, passing Player orientation in to set the various Primitive properties.
- Generates clipped polygons from the World as required.  This is done in ``CellWorldDataGenerator::getWorldData()``
  and is the most complex part of the system.

  