namespace Balls.App
{

    public abstract class Screen
    {
        protected BallsApp App { get; private set; }

        public virtual void Attach(BallsApp app)
        {
            App = app;
        }

        public virtual void Enter()
        {
        }

        public virtual void Leave()
        {
        }

        public abstract void Draw();

        public virtual void HandleEscape()
        {
            App.EscapeFlow();
        }

        public virtual void DrawOverlay()
        {
        }
    }
}
