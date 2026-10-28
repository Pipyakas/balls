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
            public Dictionary<string, string> Query;
            public int Status = 200;
            public string Body = "{}";
            public bool Answered;
            public readonly ManualResetEventSlim Done = new(false);
        }

        private readonly BallsApp app;
        private readonly TcpListener listener;
        private readonly Thread thread;
        private readonly Queue<(Socket socket, Request request)> queue = new();
        private readonly List<(string path, Request request, float deadline)> captures = new();
        private volatile bool running;
        private bool quitRequested;

        public ControlApi(BallsApp app, int port)
        {
            this.app = app;
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

        private void Handle(Request request)
        {
            string path = request.Path;
            Dictionary<string, string> query = request.Query;
            switch (path)
            {
                case "/state":
                    Ok(request, StateJson());
                    return;
                case "/screenshot":
                    string capture = Get(query, "path");
                    if (string.IsNullOrEmpty(capture))
                    {
                        Error(request, 400, "missing path");
                        return;
                    }
                    Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(capture)) ?? ".");
                    ScreenCapture.CaptureScreenshot(capture);
                    // Answered by CompleteCaptures once the file is on disk.
                    request.Answered = false;
                    return;
                case "/play":
                {
                    int mode = ModeFrom(Get(query, "mode"));
                    string level = Get(query, "level") ?? string.Empty;
                    if (mode < 0 || level.Length == 0)
                    {
                        Error(request, 400, "mode must be normal or performance, and level is required");
                        return;
                    }
                    if (!File.Exists(Path.Combine(app.LevelDirectoryFor(mode), level + "_Bricks.txt")))
                    {
                        Error(request, 404, $"no {(mode == BallsApp.PerformanceMode ? "performance" : "normal")} level named {level}");
                        return;
                    }
                    app.StartGameplay(mode, level, app.DefaultBackendKind);
                    Ok(request, "{\"ok\":true}");
                    return;
                }
                case "/levels":
                {
                    int mode = ModeFrom(Get(query, "mode"));
                    if (mode < 0)
                    {
                        Error(request, 400, "mode must be normal or performance");
                        return;
                    }
                    app.SetMode(mode);
                    app.Go<Screens.LevelSelectScreen>();
                    Ok(request, "{\"ok\":true}");
                    return;
                }
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
                        return;
                    }
                    var levels = app.Get<Screens.LevelSelectScreen>();
                    bool imported = levels.Import(picked);
                    Ok(request, $"{{\"ok\":{(imported ? "true" : "false")},\"toast\":{Json(app.ToastText)}}}");
                    return;
                }
                case "/menu":
                    app.Go<Screens.MainMenuScreen>();
                    Ok(request, "{\"ok\":true}");
                    return;
                case "/quit":
                    // Answered first: quitting inside the handler would take the frame down before the reply is sent.
                    quitRequested = true;
                    Ok(request, "{\"ok\":true}");
                    return;
                case "/start":
                    if (!app.Running)
                    {
                        Error(request, 409, "no game running");
                        return;
                    }
                    BallsInput.RequestAutoRelease();
                    Ok(request, "{\"ok\":true}");
                    return;
                case "/launcher":
                {
                    Screens.GameplayScreen gameplay = Gameplay();
                    if (gameplay == null)
                    {
                        Error(request, 409, "no game running");
                        return;
                    }
                    int column = Integer(query, "column", 0);
                    int row = Integer(query, "row", 0);
                    BallsInput.ClickLauncher(column, row);
                    Ok(request, "{\"ok\":true}");
                    return;
                }
                case "/rate":
                case "/multiplier":
                case "/limit":
                {
                    Screens.GameplayScreen gameplay = Gameplay();
                    if (gameplay == null)
                    {
                        Error(request, 409, "no game running");
                        return;
                    }
                    int index = Integer(query, "index", -1);
                    if (index < 0)
                    {
                        Error(request, 400, "index is required");
                        return;
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
                    return;
                }
                case "/backend":
                {
                    string kind = Get(query, "kind") ?? string.Empty;
                    if (!BallsNative.TryBackendKind(kind, out int backendKind) || !BallsNative.BackendAvailable(backendKind))
                    {
                        Error(request, 409, "backend unknown or unavailable");
                        return;
                    }
                    if (app.Running)
                    {
                        app.SwitchBackend(backendKind);
                    }
                    else
                    {
                        app.SetBackend(backendKind);
                    }
                    Ok(request, "{\"ok\":true}");
                    return;
                }
                case "/pause":
                case "/resume":
                {
                    Screens.GameplayScreen gameplay = Gameplay();
                    if (gameplay == null)
                    {
                        Error(request, 409, "no game running");
                        return;
                    }
                    gameplay.SetPaused(path == "/pause");
                    Ok(request, "{\"ok\":true}");
                    return;
                }
                case "/restart":
                {
                    if (!app.Running)
                    {
                        Error(request, 409, "no game running");
                        return;
                    }
                    app.RestartGameplay();
                    Ok(request, "{\"ok\":true}");
                    return;
                }
                case "/settings":
                    Settings(request);
                    return;
                case "/benchmark":
                    if (Integer(query, "run", 0) != 0)
                    {
                        app.Go<Screens.BenchmarkScreen>();
                        Ok(request, "{\"ok\":true,\"running\":true}");
                        return;
                    }
                    Ok(request, BenchmarkJson());
                    return;
                case "/compute-gpu":
                {
                    string wanted = Get(query, "id") ?? Get(query, "name") ?? "auto";
                    if (!BallsNative.SetComputeGpu(wanted))
                    {
                        Error(request, 400, "no compute GPU matches that id or name");
                        return;
                    }
                    Ok(request, "{\"ok\":true}");
                    return;
                }
                default:
                    Error(request, 404, "unknown path");
                    return;
            }
        }

        /// <summary>The game's Settings fields, applied the way the Settings screen applies them.</summary>
        private void Settings(Request request)
        {
            SettingsData settings = app.Settings;
            if (settings == null)
            {
                Error(request, 500, "settings are not loaded");
                return;
            }
            Dictionary<string, string> query = request.Query;
            if (query.TryGetValue("collisions", out string collisions))
            {
                settings.BallCollisions = Integer(query, "collisions", 0) != 0;
            }
            if (query.TryGetValue("vsync", out _))
            {
                settings.Vsync = Integer(query, "vsync", 0) != 0;
                QualitySettings.vSyncCount = settings.Vsync ? 1 : 0;
            }
            if (query.TryGetValue("layout", out string layout))
            {
                switch (layout.ToLowerInvariant())
                {
                    case "auto":
                        settings.Orientation = SettingsData.OrientationAuto;
                        break;
                    case "landscape":
                        settings.Orientation = SettingsData.OrientationLandscape;
                        break;
                    case "portrait":
                        settings.Orientation = SettingsData.OrientationPortrait;
                        break;
                    default:
                        Error(request, 400, "layout must be auto, landscape or portrait");
                        return;
                }
                app.ApplyOrientation();
                app.Get<Screens.GameplayScreen>().RefreshLayout();
            }
            if (query.TryGetValue("backend", out string backend))
            {
                if (!BallsNative.TryBackendKind(backend, out int kind) || !BallsNative.BackendAvailable(kind))
                {
                    Error(request, 409, "backend unknown or unavailable");
                    return;
                }
                settings.DefaultBackend = BallsNative.BackendName(kind);
                app.SetBackend(kind);
            }
            if (Get(query, "advanced") is string advanced)
            {
                settings.AdvancedSettings = Integer(query, "advanced", 0) != 0;
                app.Get<Screens.GameplayScreen>().SetAdvancedOpen(settings.AdvancedSettings);
            }
            if (query.ContainsKey("collisions"))
            {
                app.ApplyBallCollisions(settings.BallCollisions);
            }
            app.SaveSettings();
            if (Integer(query, "open", 0) != 0)
            {
                app.Go<Screens.SettingsScreen>();
            }
            if (Get(query, "scroll") is string scroll && scroll.Equals("end", StringComparison.OrdinalIgnoreCase))
            {
                app.Get<Screens.SettingsScreen>().ScrollToEnd();
                app.Get<Screens.GameplayScreen>().ScrollControlsToEnd();
            }
            Ok(request, SettingsJson(settings));
        }

        private string StateJson()
        {
            var text = new StringBuilder(256);
            text.Append("{\"ok\":true,\"screen\":").Append(Json(app.ScreenName));
            text.Append(",\"mode\":").Append(Json(app.Mode == BallsApp.PerformanceMode ? "performance" : "normal"));
            text.Append(",\"level\":").Append(Json(app.LevelName));
            text.Append(",\"running\":").Append(app.Running ? "true" : "false");
            // A finished game leaves the session component behind, so its fields only mean something while one runs.
            if (app.Running && app.TryReadSession(out BoardInfoData board, out SessionInfo session, out SessionStatsData stats))
            {
                Screens.GameplayScreen gameplay = Gameplay();
                text.Append(",\"state\":").Append(Json(StateName(session.state)));
                text.Append(",\"paused\":").Append(gameplay != null && gameplay.Paused ? "true" : "false");
                text.Append(",\"backend\":").Append(Json(BallsNative.BackendName(session.backend)));
                text.Append(",\"pendingBackend\":")
                    .Append(session.pendingBackend >= 0 ? Json(BallsNative.BackendName(session.pendingBackend)) : "null");
                text.Append(",\"renderer\":").Append(Json(BallsApp.RendererText));
                text.Append(",\"balls\":").Append(stats.liveBalls.ToString(CultureInfo.InvariantCulture));
                text.Append(",\"bricks\":").Append(board.aliveBricks.ToString(CultureInfo.InvariantCulture));
                text.Append(",\"fps\":").Append(stats.fps.ToString("F1", CultureInfo.InvariantCulture));
                text.Append(",\"frameMs\":").Append(stats.frameMs.ToString("F2", CultureInfo.InvariantCulture));
                text.Append(",\"physicsMs\":").Append(stats.physicsMs.ToString("F2", CultureInfo.InvariantCulture));
                text.Append(",\"collisions\":").Append(stats.collisions ? "true" : "false");
            }
            text.Append(",\"toast\":").Append(Json(app.ToastText));
            text.Append(",\"screenWidth\":").Append(UnityEngine.Screen.width);
            text.Append(",\"screenHeight\":").Append(UnityEngine.Screen.height);
            text.Append(",\"fullScreen\":").Append(UnityEngine.Screen.fullScreen ? "true" : "false");
            text.Append(",\"orientation\":").Append(Json(app.Settings?.OrientationName.ToLowerInvariant() ?? "auto"));
            text.Append('}');
            return text.ToString();
        }

        private string BenchmarkJson()
        {
            var text = new StringBuilder(128);
            text.Append("{\"ok\":true,\"scores\":[");
            bool first = true;
            for (int kind = 0; kind < BallsNative.BackendCount; ++kind)
            {
                if (!app.Profile.TryScore(BallsNative.BackendName(kind), out double score))
                {
                    continue;
                }
                if (!first)
                {
                    text.Append(',');
                }
                first = false;
                text.Append("{\"backend\":").Append(Json(BallsNative.BackendName(kind)));
                text.Append(",\"ballStepsPerSecond\":").Append(score.ToString("F0", CultureInfo.InvariantCulture));
                text.Append('}');
            }
            text.Append("],\"winner\":")
                .Append(app.Profile.TryScore(app.Settings?.DefaultBackend ?? string.Empty, out _)
                    ? Json(app.Settings.DefaultBackend) : "null");
            text.Append('}');
            return text.ToString();
        }

        private static string SettingsJson(SettingsData settings)
        {
            var text = new StringBuilder(128);
            text.Append("{\"ok\":true,\"collisions\":").Append(settings.BallCollisions ? "true" : "false");
            text.Append(",\"vsync\":").Append(settings.Vsync ? "true" : "false");
            text.Append(",\"layout\":").Append(Json(settings.OrientationName.ToLowerInvariant()));
            text.Append(",\"backend\":").Append(Json(settings.DefaultBackend));
            text.Append('}');
            return text.ToString();
        }

        private Screens.GameplayScreen Gameplay() => app.Running ? app.Get<Screens.GameplayScreen>() : null;

        private static string StateName(int state) => state switch
        {
            BallsNative.StateRunning => "running",
            BallsNative.StateWon => "won",
            _ => "ready",
        };

        private static int ModeFrom(string mode) => mode?.ToLowerInvariant() switch
        {
            "normal" => BallsApp.NormalMode,
            "performance" => BallsApp.PerformanceMode,
            _ => -1,
        };

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
