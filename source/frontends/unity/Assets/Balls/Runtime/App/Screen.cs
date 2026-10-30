namespace Balls.App
{
    /// <summary>
    /// One of the game's screens. Screens draw in layout units and own no state beyond their own widgets: the level,
    /// the mode and the session live in <see cref="BallsApp"/> and the ECS world.
    /// </summary>
    public abstract class Screen
    {
        protected BallsApp App { get; private set; }

        public virtual void Attach(BallsApp app)
        {
            App = app;
        }

        /// <summary>Called once when the screen becomes current.</summary>
        public virtual void Enter()
        {
        }

        /// <summary>Called when another screen replaces this one.</summary>
        public virtual void Leave()
        {
        }

        /// <summary>Draws and handles input. Runs inside OnGUI.</summary>
        public abstract void Draw();

        /// <summary>Handles Esc as the screen's Back action.</summary>
        public virtual void HandleEscape()
        {
            App.EscapeFlow();
        }

        /// <summary>Draws on top of the screen (pause, win, settings, load error).</summary>
        public virtual void DrawOverlay()
        {
        }
    }
}
