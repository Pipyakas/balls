using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using Balls.Ecs;
using Balls.Native;
using Balls.Ui;
using UnityEngine;

namespace Balls.App
{
    /// <summary>
    /// The game's HTTP control API, the surface benchmarks and conformance runs drive (<c>/state</c>, <c>/screenshot</c>,
    /// <c>/play</c>, <c>/start</c>, <c>/rate</c>, <c>/backend</c>, <c>/settings</c>, <c>/benchmark</c>, ...).
    ///
    /// Connections are accepted on a background thread, but every request is answered from <see cref="Pump"/> on the
    /// main thread: the session, the screens and Unity's APIs are not thread-safe, and the SDL front end makes the same
    /// split between its socket thread and the frame.
    /// </summary>
    public sealed class ControlApi : IDisposable
    {
        private const int ListenBacklog = 16;

        private sealed class Request
        {
            public string Path;
            public string Target;
            public Dictionary<string, string> Query;
            public int Status = 200;
            public string Body = "{}";
            public bool Answered;
            public readonly ManualResetEventSlim Done = new(false);
        }

        private readonly BallsApp app;
        // The core's handler; levels resolve against the same folders the level select uses.
        private IntPtr control;
        private readonly TcpListener listener;
        private readonly Thread thread;
        private readonly Queue<(Socket socket, Request request)> queue = new();
        private readonly List<(string path, Request request, float deadline)> captures = new();
        private volatile bool running;
        private bool quitRequested;

        public ControlApi(BallsApp app, int port)
        {
            this.app = app;
            control = BallsNative.balls_control_create(app.LevelDirectoryFor(BallsApp.NormalMode),
                app.LevelDirectoryFor(BallsApp.PerformanceMode));
            listener = new TcpListener(IPAddress.Loopback, port);
            thread = new Thread(AcceptLoop) { IsBackground = true, Name = "balls-control" };
        }

        /// <summary>The port asked for, or 0 when the listener could not take it.</summary>
        public int Port { get; private set; }

        public void Start()
        {
            try
            {
                listener.Start(ListenBacklog);
            }
            catch (SocketException error)
            {
                Debug.LogWarning($"balls: control API cannot listen: {error.Message}");
                return;
            }
            Port = ((IPEndPoint)listener.LocalEndpoint).Port;
            running = true;
            thread.Start();
            Debug.Log($"balls: control API on http://127.0.0.1:{Port}/");
        }

        public void Dispose()
        {
            running = false;
            listener?.Stop();
            BallsNative.balls_control_destroy(control);
            control = IntPtr.Zero;
        }

        /// <summary>Answers whatever arrived since the last frame, once per frame from the main thread.</summary>
        public void Pump()
        {
            while (TryDequeue(out Socket socket, out Request request))
            {
                Handle(request);
                Debug.Log($"balls: control {request.Path} -> {request.Status}");
                if (request.Answered)
                {
                    Respond(socket, request);
                }
                else
                {
                    // Deferred (a screenshot, which Unity finishes at the end of the frame): answer it there.
                    captures.Add((Get(request.Query, "path") ?? string.Empty, request,
                        Time.realtimeSinceStartup + 10f));
                    pendingSockets[request] = socket;
                }
            }
            CompleteCaptures();
            if (quitRequested)
            {
                quitRequested = false;
                Dispose();
                Application.Quit();
            }
        }

        private readonly Dictionary<Request, Socket> pendingSockets = new();

        private void CompleteCaptures()
        {
            float now = Time.realtimeSinceStartup;
            for (int index = captures.Count - 1; index >= 0; --index)
            {
                (string path, Request request, float deadline) = captures[index];
                bool written = path.Length > 0 && File.Exists(path);
                if (!written && now < deadline)
                {
                    continue;
                }
                captures.RemoveAt(index);
                if (written)
                {
                    request.Status = 200;
                    request.Body = $"{{\"ok\":true,\"path\":{Json(path)}}}";
                }
                else
                {
                    request.Status = 500;
                    request.Body = "{\"ok\":false,\"error\":\"screenshot was not written\"}";
                }
                request.Answered = true;
                if (pendingSockets.Remove(request, out Socket socket))
                {
                    Respond(socket, request);
                }
            }
        }

        /// <summary>
        /// Routes Unity keeps (import, compute GPU and the gameplay controls, which go through the HUD and BallsInput so
        /// they work in Burst too) are answered here; everything else is the core's handler, the same one SDL and
        /// Unreal use, and what it asks of the front end comes back as actions.
        /// </summary>
        private void Handle(Request request)
        {
            if (HandleOwn(request))
            {
                return;
            }
            NativeControlContext context = Context();
            int status = BallsNative.balls_control_handle(control, ref context, request.Target, json, json.Length, out int length);
            if (length > json.Length)
            {
                json = new byte[length + 1];
                status = BallsNative.balls_control_handle(control, ref context, request.Target, json, json.Length, out length);
            }
            if (status > 0)
            {
                request.Status = status;
                request.Body = Encoding.UTF8.GetString(json, 0, length);
                request.Answered = true;
            }
            else if (status < 0)
            {
                Error(request, 404, "unknown path");
            }
            RunActions(request);
        }

        private byte[] json = new byte[65536];

        private bool HandleOwn(Request request)
        {
            string path = request.Path;
            Dictionary<string, string> query = request.Query;
            switch (path)
            {
                case "/import":
                {
                    // Several picks travel in one parameter, separated by '|', the way a drop hands over a folder.
                    var picked = new List<string>();
                    foreach (string entry in (Get(query, "path") ?? string.Empty).Split('|', StringSplitOptions.RemoveEmptyEntries))
                    {
                        picked.Add(entry);
                    }
                    if (picked.Count == 0)
                    {
                        Error(request, 400, "missing path");
                        return true;
                    }
                    var levels = app.Get<Screens.LevelSelectScreen>();
                    bool imported = levels.Import(picked);
                    Ok(request, $"{{\"ok\":{(imported ? "true" : "false")},\"toast\":{Json(app.ToastText)}}}");
                    return true;
                }
                case "/compute-gpu":
                {
                    string wanted = Get(query, "id") ?? Get(query, "name") ?? "auto";
                    if (!BallsNative.SetComputeGpu(wanted))
                    {
                        Error(request, 400, "no compute GPU matches that id or name");
                        return true;
                    }
                    Ok(request, "{\"ok\":true}");
                    return true;
                }
            }
            Screens.GameplayScreen gameplay = Gameplay();
            if (gameplay == null)
            {
                return false;
            }
            switch (path)
            {
                case "/start":
                    BallsInput.RequestAutoRelease();
                    Ok(request, "{\"ok\":true}");
                    return true;
                case "/launch":
                case "/launcher":
                    BallsInput.ClickLauncher(Integer(query, "column", 0), Integer(query, "row", 0));
                    Ok(request, "{\"ok\":true}");
                    return true;
                case "/rate":
                case "/multiplier":
                case "/limit":
                {
                    int index = Integer(query, "index", -1);
                    if (index < 0)
                    {
                        Error(request, 400, "index is required");
                        return true;
                    }
                    if (path == "/rate")
                    {
                        gameplay.ControlSpawnRate(index);
                    }
                    else if (path == "/multiplier")
                    {
                        gameplay.ControlMultiplier(index);
                    }
                    else
                    {
                        gameplay.ControlBallLimit(index);
                    }
                    Ok(request, "{\"ok\":true}");
                    return true;
                }
                case "/pause":
                case "/resume":
                    gameplay.SetPaused(path == "/pause");
                    Ok(request, "{\"ok\":true}");
                    return true;
                case "/restart":
                    app.RestartGameplay();
                    Ok(request, "{\"ok\":true}");
                    return true;
                case "/backend":
                {
                    string kind = Get(query, "kind") ?? string.Empty;
                    if (!BallsNative.TryBackendKind(kind, out int backendKind) || !BallsNative.BackendAvailable(backendKind))
                    {
                        Error(request, 409, "backend unknown or unavailable");
                        return true;
                    }
                    app.SwitchBackend(backendKind);
                    Ok(request, "{\"ok\":true}");
                    return true;
                }
            }
            return false;
        }

        /// <summary>What the core reads for one request; the strings and handles only live for the call.</summary>
        private NativeControlContext Context()
        {
            Screens.GameplayScreen gameplay = Gameplay();
            SessionSystem system = SessionSystem.Current;
            IntPtr session = gameplay != null && system?.Backend is Game.NativeGameBackend native ? native.SessionHandle : IntPtr.Zero;
            IntPtr benchmark = app.ScreenName == "benchmark" ? app.Get<Screens.BenchmarkScreen>().Handle : IntPtr.Zero;
            return new NativeControlContext
            {
                renderer = BallsApp.RendererText,
                screen = app.ScreenName,
                windowWidth = UnityEngine.Screen.width,
                windowHeight = UnityEngine.Screen.height,
                preferredBackend = app.DefaultBackendKind,
                vsync = QualitySettings.vSyncCount > 0 ? 1 : 0,
                toast = app.ToastText,
                extraFields = $"\"fullScreen\":{(UnityEngine.Screen.fullScreen ? "true" : "false")}," +
                              $"\"orientation\":{Json(app.Settings?.OrientationName.ToLowerInvariant() ?? "auto")}",
                screenDetails = string.Empty,
                frames = system?.FrameTimingsHandle ?? IntPtr.Zero,
                settings = app.Settings?.ToNative() ?? new NativeSettings { defaultBackend = new byte[64] },
                session = session,
                mode = app.Mode,
                levelName = app.LevelName ?? string.Empty,
                loadError = system?.LoadFailure ?? string.Empty,
                diskState = gameplay != null ? (int)gameplay.WatchState : 0,
                paused = gameplay != null && gameplay.Paused ? 1 : 0,
                probeTargetFps = gameplay?.ProbeTargetFps ?? 60,
                benchmark = benchmark,
                profile = app.Profile.Handle,
            };
        }

        /// <summary>Carries out what the core's answer asked of Unity, in order.</summary>
        private void RunActions(Request request)
        {
            while (BallsNative.balls_control_take_action(control, out NativeControlAction action) != BallsNative.ControlNone)
            {
                switch (action.kind)
                {
                    case BallsNative.ControlScreenshot:
                        string capture = action.Text;
                        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(capture)) ?? ".");
                        ScreenCapture.CaptureScreenshot(capture);
                        // Answered by CompleteCaptures once the file is on disk.
                        request.Answered = false;
                        break;
                    case BallsNative.ControlPlay:
                        app.StartGameplay(action.mode, action.Text, app.DefaultBackendKind);
                        break;
                    case BallsNative.ControlMainMenu:
                        app.Go<Screens.MainMenuScreen>();
                        break;
                    case BallsNative.ControlLevelSelect:
                        app.SetMode(action.mode);
                        app.Go<Screens.LevelSelectScreen>();
                        break;
                    case BallsNative.ControlEditor:
                        app.SetMode(action.mode);
                        if (action.Text.Length > 0)
                        {
                            app.Get<Screens.EditorScreen>().StartEditing(action.mode, action.Text);
                        }
                        else
                        {
                            app.Get<Screens.EditorScreen>().StartNewLevel(action.mode);
                        }
                        app.Go<Screens.EditorScreen>();
                        break;
                    case BallsNative.ControlQuit:
                        // Answered first: quitting inside the handler would take the frame down before the reply is sent.
                        quitRequested = true;
                        break;
                    case BallsNative.ControlApplySettings:
                        ApplySettings(action, request.Query);
                        break;
                    case BallsNative.ControlOpenSettings:
                        app.Go<Screens.SettingsScreen>();
                        break;
                    case BallsNative.ControlSetVsync:
                        if (app.Settings != null)
                        {
                            app.Settings.Vsync = action.value != 0;
                            app.SaveSettings();
                        }
                        QualitySettings.vSyncCount = action.value != 0 ? 1 : 0;
                        break;
                    case BallsNative.ControlRunBenchmark:
                        app.Go<Screens.BenchmarkScreen>();
                        break;
                    case BallsNative.ControlSkipBenchmark:
                    case BallsNative.ControlContinueBenchmark:
                        if (app.ScreenName == "benchmark")
                        {
                            app.Get<Screens.BenchmarkScreen>().ControlContinue();
                        }
                        break;
                    case BallsNative.ControlSetPaused:
                        Gameplay()?.SetPaused(action.value != 0);
                        break;
                    case BallsNative.ControlRestart:
                        app.RestartGameplay();
                        break;
                    case BallsNative.ControlStartProbe:
                        Gameplay()?.ControlProbe(action.value);
                        break;
                }
            }
        }

        /// <summary>The core's checked settings, applied the way the Settings screen applies them.</summary>
        private void ApplySettings(NativeControlAction action, Dictionary<string, string> query)
        {
            SettingsData settings = app.Settings;
            if (settings == null)
            {
                return;
            }
            NativeSettings next = action.settings;
            bool collisionsChanged = settings.BallCollisions != (next.ballCollisions != 0);
            bool layoutChanged = settings.Orientation != next.orientation;
            settings.BallCollisions = next.ballCollisions != 0;
            settings.Orientation = next.orientation;
            settings.Vsync = next.vsync != 0;
            QualitySettings.vSyncCount = settings.Vsync ? 1 : 0;
            if (settings.AdvancedSettings != (next.advancedSettings != 0))
            {
                settings.AdvancedSettings = next.advancedSettings != 0;
                app.Get<Screens.GameplayScreen>().SetAdvancedOpen(settings.AdvancedSettings);
            }
            if (action.value >= 0)
            {
                settings.DefaultBackend = BallsNative.StoredBackendName(action.value);
                app.SetBackend(action.value);
            }
            if (layoutChanged)
            {
                app.ApplyOrientation();
                app.Get<Screens.GameplayScreen>().RefreshLayout();
            }
            if (collisionsChanged)
            {
                app.ApplyBallCollisions(settings.BallCollisions);
            }
            app.SaveSettings();
            // Unity's own addition for screenshots of the long lists.
            if (Get(query, "scroll") is string scroll && scroll.Equals("end", StringComparison.OrdinalIgnoreCase))
            {
                app.Get<Screens.SettingsScreen>().ScrollToEnd();
                app.Get<Screens.GameplayScreen>().ScrollControlsToEnd();
            }
        }

        private Screens.GameplayScreen Gameplay() => app.Running ? app.Get<Screens.GameplayScreen>() : null;

        private static string Get(Dictionary<string, string> query, string name) =>
            query.TryGetValue(name, out string value) ? value : null;

        private static int Integer(Dictionary<string, string> query, string name, int fallback) =>
            query.TryGetValue(name, out string text) && int.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture,
                out int value) ? value : fallback;

        private static void Ok(Request request, string body)
        {
            request.Status = 200;
            request.Body = body;
            request.Answered = true;
        }

        private static void Error(Request request, int status, string message)
        {
            request.Status = status;
            request.Body = $"{{\"ok\":false,\"error\":{Json(message)}}}";
            request.Answered = true;
        }

        private static string Json(string text)
        {
            var escaped = new StringBuilder(text.Length + 2);
            escaped.Append('"');
            foreach (char letter in text)
            {
                switch (letter)
                {
                    case '"': escaped.Append("\\\""); break;
                    case '\\': escaped.Append("\\\\"); break;
                    case '\n': escaped.Append("\\n"); break;
                    case '\r': escaped.Append("\\r"); break;
                    case '\t': escaped.Append("\\t"); break;
                    default:
                        if (letter < ' ')
                        {
                            escaped.Append("\\u").Append(((int)letter).ToString("x4"));
                        }
                        else
                        {
                            escaped.Append(letter);
                        }
                        break;
                }
            }
            escaped.Append('"');
            return escaped.ToString();
        }

        private bool TryDequeue(out Socket socket, out Request request)
        {
            lock (queue)
            {
                if (queue.Count > 0)
                {
                    (socket, request) = queue.Dequeue();
                    return true;
                }
            }
            socket = null;
            request = null;
            return false;
        }

        private void AcceptLoop()
        {
            Debug.Log("balls: control accept loop started");
            while (running)
            {
                Socket socket;
                try
                {
                    socket = listener.AcceptSocket();
                }
                catch (SocketException)
                {
                    return;
                }
                catch (ObjectDisposedException)
                {
                    return;
                }
                ThreadPool.QueueUserWorkItem(_ => Serve(socket));
            }
        }

        private void Serve(Socket socket)
        {
            try
            {
                socket.ReceiveTimeout = 5000;
                var buffer = new byte[8192];
                int length = 0;
                while (length < buffer.Length)
                {
                    int read = socket.Receive(buffer, length, buffer.Length - length, SocketFlags.None);
                    if (read <= 0)
                    {
                        break;
                    }
                    length += read;
                    if (Encoding.ASCII.GetString(buffer, 0, length).Contains("\r\n\r\n"))
                    {
                        break;
                    }
                }
                string head = Encoding.ASCII.GetString(buffer, 0, length);
                string firstLine = head.Split('\n')[0].Trim();
                string[] parts = firstLine.Split(' ');
                if (parts.Length < 2)
                {
                    socket.Close();
                    return;
                }
                string target = parts[1];
                int queryStart = target.IndexOf('?');
                var request = new Request
                {
                    Path = queryStart >= 0 ? target.Substring(0, queryStart) : target,
                    Target = target,
                    Query = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase),
                };
                if (queryStart >= 0)
                {
                    foreach (string pair in target.Substring(queryStart + 1).Split('&', StringSplitOptions.RemoveEmptyEntries))
                    {
                        int equals = pair.IndexOf('=');
                        string name = equals >= 0 ? pair.Substring(0, equals) : pair;
                        string value = equals >= 0 ? pair.Substring(equals + 1) : string.Empty;
                        request.Query[Uri.UnescapeDataString(name)] = Uri.UnescapeDataString(value.Replace('+', ' '));
                    }
                }
                lock (queue)
                {
                    queue.Enqueue((socket, request));
                }
                if (!request.Done.Wait(TimeSpan.FromSeconds(15)))
                {
                    socket.Close();
                }
            }
            catch (Exception error)
            {
                Debug.LogWarning($"balls: control request failed: {error.Message}");
                try
                {
                    socket.Close();
                }
                catch (Exception)
                {
                    // The socket is already gone; nothing to report.
                }
            }
        }

        private void Respond(Socket socket, Request request)
        {
            try
            {
                byte[] body = Encoding.UTF8.GetBytes(request.Body);
                var head = new StringBuilder(128);
                head.Append("HTTP/1.1 ").Append(request.Status).Append(' ')
                    .Append(request.Status == 200 ? "OK" : "Error").Append("\r\n");
                head.Append("Content-Type: application/json\r\n");
                head.Append("Content-Length: ").Append(body.Length).Append("\r\n");
                head.Append("Connection: close\r\n\r\n");
                socket.Send(Encoding.ASCII.GetBytes(head.ToString()));
                socket.Send(body);
            }
            catch (SocketException)
            {
                // The client hung up; the request has already been applied.
            }
            finally
            {
                socket.Close();
                request.Done.Set();
            }
        }
    }
}
