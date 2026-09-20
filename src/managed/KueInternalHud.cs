using System;
using System.Collections;
using System.Collections.Generic;
using System.Reflection;
using System.Runtime.CompilerServices;
using GameNetcodeStuff;
using Unity.Netcode;
using UnityEngine;
using UnityEngine.AI;
using UnityEngine.Experimental.Rendering;
using UnityEngine.InputSystem;
using UnityEngine.Rendering;
using UnityEngine.Rendering.HighDefinition;
using UnityEngine.UI;

namespace Kue.Internal
{
    internal enum CatalogBeginResult
    {
        Begun,
        TransactionInProgress,
        GenerationExhausted
    }

    internal enum CatalogReportResult
    {
        ManagedNameUnavailable = -1,
        None,
        Recorded,
        AlreadyRecorded,
        NoTransaction,
        WrongTransaction,
        PriorReportFailed,
        CapacityExceeded,
        InvalidAsset,
        EmptyName,
        EmbeddedNull,
        InvalidUtf8,
        NameTooLong,
        AssetNameConflict,
        DuplicateName
    }

    internal enum CatalogCommitResult
    {
        Committed,
        Unchanged,
        NoTransaction,
        WrongTransaction,
        PriorReportFailed,
        EmptyCatalog
    }

    internal enum EnemyActivityBeginResult
    {
        Begun,
        TransactionInProgress,
        NoCatalog,
        StaleGeneration
    }

    internal enum EnemyActivityReportResult
    {
        None,
        Recorded,
        NoTransaction,
        WrongTransaction,
        PriorReportFailed,
        StaleGeneration,
        IndexOutOfRange,
        ZeroCount,
        CountOverflow
    }

    internal enum EnemyActivityCommitResult
    {
        Changed,
        Unchanged,
        NoTransaction,
        WrongTransaction,
        PriorReportFailed
    }

    internal static class NativeBridge
    {
        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern IntPtr RenderMenu(int screenWidth, int screenHeight, float mouseX,
                                                 float mouseY, int mouseDown, float wheel,
                                                 out int x, out int y, out int width,
                                                 out int height, out int byteCount,
                                                 out ulong pixelRevision);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void GetHudConfig(out int flags, out float maxDistance,
                                                 out int menuKey, out int tickIntervalMs,
                                                 out int revision);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void GetEspColor(int category, out float r, out float g, out float b,
                                                out float a);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern int PollAction(out int targetClientId);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void ResetRuntimeCatalogs();

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void ReportPersistentLureTarget(int targetClientId);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern CatalogBeginResult BeginEnemyCatalog();

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern CatalogReportResult ReportEnemyType(int instanceId, string name);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern CatalogCommitResult CommitEnemyCatalog();

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void AbortEnemyCatalog();

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern EnemyActivityBeginResult BeginActiveEnemyTypes();

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern EnemyActivityReportResult ReportActiveEnemyType(int index,
                                                                                int count);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern EnemyActivityCommitResult CommitActiveEnemyTypes();

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void AbortActiveEnemyTypes();

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void ReportFlyState(int enabled);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void ReportHostState(int isHost);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern CatalogBeginResult BeginItemCatalog();

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern CatalogReportResult ReportItemType(int instanceId, string name);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern CatalogCommitResult CommitItemCatalog();

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void AbortItemCatalog();

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern CatalogBeginResult BeginMoonCatalog();

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern CatalogReportResult ReportMoonType(int instanceId, string name);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern CatalogCommitResult CommitMoonCatalog();

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void AbortMoonCatalog();
    }

    public static class HudBootstrap
    {
        private static GameObject host;

        public static void Install()
        {
            if (host != null || UnityEngine.Object.FindObjectOfType<KueHud>() != null)
                return;
            host = new GameObject("Kue.InternalHud");
            UnityEngine.Object.DontDestroyOnLoad(host);
            host.hideFlags = HideFlags.HideAndDontSave;
            host.AddComponent<KueHud>();
            Debug.Log("[Kue] Internal Unity/ImGui HUD installed");
        }
    }

    public sealed class KueHud : MonoBehaviour
    {
        private enum MarkKind
        {
            Static,
            Player,
            Enemy,
            Item,
        }

        private sealed class Mark
        {
            public string name;
            public MarkKind kind;
            public Component owner;
            public Transform marker;
            public Renderer[] renderers;
            public Renderer[] playerRenderers;
            public Color color;
            public int value = -1;
            public bool enabled;
            public bool portal;
            public float rendererRadius;
            public string labelText;
            public string labelName;
            public int labelValue = int.MinValue;
            public int labelDistance = int.MinValue;
            public int labelFlags = -1;
        }

        private sealed class ModelHighlightPass : CustomPass
        {
            public KueHud owner;

            protected override void Execute(CustomPassContext context)
            {
                if (owner != null)
                    owner.ExecuteModelHighlight(context);
            }
        }

        private struct HighlightDraw
        {
            public Renderer renderer;
            public Material material;
            public int submeshCount;
        }

        private sealed class RendererCacheEntry
        {
            public Component owner;
            public Renderer[] renderers;
        }

        private sealed class MeshCacheEntry
        {
            public Renderer owner;
            public MeshFilter filter;
            public Mesh mesh;
            public Vector3[] vertices;
        }

        private sealed class ObjectCacheEntry
        {
            public UnityEngine.Object[] objects;
            public float expiresAt;
        }

        private struct MethodCacheKey : IEquatable<MethodCacheKey>
        {
            public Type targetType;
            public string name;
            public Type firstArgument;
            public Type secondArgument;

            public bool Equals(MethodCacheKey other)
            {
                return targetType == other.targetType && name == other.name &&
                       firstArgument == other.firstArgument &&
                       secondArgument == other.secondArgument;
            }

            public override bool Equals(object value)
            {
                return value is MethodCacheKey && Equals((MethodCacheKey)value);
            }

            public override int GetHashCode()
            {
                int hash = targetType.GetHashCode();
                hash = (hash * 397) ^ name.GetHashCode();
                hash = (hash * 397) ^ (firstArgument == null ? 0 : firstArgument.GetHashCode());
                return (hash * 397) ^ (secondArgument == null ? 0 : secondArgument.GetHashCode());
            }
        }

        private struct MemberAccessor
        {
            public FieldInfo field;
            public PropertyInfo property;
        }

        private struct MemberCacheKey : IEquatable<MemberCacheKey>
        {
            public Type targetType;
            public string name;

            public bool Equals(MemberCacheKey other)
            {
                return targetType == other.targetType && name == other.name;
            }

            public override bool Equals(object value)
            {
                return value is MemberCacheKey && Equals((MemberCacheKey)value);
            }

            public override int GetHashCode()
            {
                return (targetType.GetHashCode() * 397) ^ name.GetHashCode();
            }
        }

        private const float MinimumLocalDeathRoundTime = 2f;

        private readonly List<Mark> marks = new List<Mark>();
        private readonly List<Vector2> projectedVertices = new List<Vector2>(1024);
        private readonly List<Vector2> hullVertices = new List<Vector2>(1024);
        private readonly Vector3[] portalWorldCorners = new Vector3[4];
        private readonly Vector2[] portalScreenCorners = new Vector2[4];
        private readonly List<EnemyType> enemyCatalogBuffer = new List<EnemyType>();
        private readonly List<Item> itemCatalogBuffer = new List<Item>();
        private readonly List<SelectableLevel> moonCatalogBuffer = new List<SelectableLevel>();
        private readonly List<int> moonLevelBuffer = new List<int>();
        private readonly HashSet<int> catalogInstanceIds = new HashSet<int>();
        private readonly HashSet<string> usedCatalogNames = new HashSet<string>();
        private readonly Dictionary<EnemyType, int> enemyCatalogIndices =
            new Dictionary<EnemyType, int>();
        private readonly Dictionary<string, int> enemyCatalogNames =
            new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase);
        private readonly Dictionary<int, RendererCacheEntry> rendererCache =
            new Dictionary<int, RendererCacheEntry>();
        private readonly Dictionary<int, MeshCacheEntry> meshCache =
            new Dictionary<int, MeshCacheEntry>();
        private readonly HashSet<int> activeRendererRoots = new HashSet<int>();
        private readonly List<int> staleCacheKeys = new List<int>();
        private int[] activeEnemyCounts = new int[0];
        private int[] reportedEnemyCounts = new int[0];
        private int markCount;

        private readonly Color[] outlineColors = new Color[6];
        private readonly List<HighlightDraw> highlightDraws = new List<HighlightDraw>();
        private readonly Dictionary<Color, Material> highlightMaterials =
            new Dictionary<Color, Material>();
        private CustomPassVolume highlightVolume;
        private Shader highlightShader;
        private bool highlightUnavailable;
        private Material highlightStampMaterial;
        private Material highlightCutoutMaterial;
        private Material highlightRingMaterial;
        private RTHandle highlightMask;
        private RTHandle highlightRing;
        private int highlightWidth;
        private int highlightHeight;
        private Mesh highlightQuad;
        private readonly List<Vector3> highlightQuadVertices = new List<Vector3>(4);
        private Vector3 highlightPixelRight;
        private Vector3 highlightPixelUp;
        private bool highlightResourcesLogged;
        private Shader highlightCutoutShader;
        private const int HighlightRingTaps = 16;
        private const float HighlightRingRadiusPixels = 3f;
        private Texture2D menuTexture;
        private ulong uploadedMenuPixelRevision;
        private Texture2D lineTexture;
        private Rect menuRect;
        private GUIStyle label;
        private GUIStyle shadow;
        private Camera activeCamera;
        private Camera[] cameraBuffer = new Camera[4];
        private float nextScan;
        private bool menuOpen;
        private bool menuCaptureActive;
        private PlayerControllerB menuCapturedPlayer;
        private bool capturedMoveDisabled;
        private bool capturedLookDisabled;
        private CursorLockMode capturedCursorLockState;
        private bool capturedCursorVisible;
        private bool menuBufferFailureReported;
        private int espFlags;
        private Key menuKey = Key.Insert;
        private int tickIntervalMs = 100;
        private float nextPlayerFeatureWrite;
        private int configRevision = -1;
        private int reportedHostState = -1;
        private float maxDistance = 1000f;
        private int lastGuiFrame = -1;
        private readonly Dictionary<ulong, bool> playerDeathState = new Dictionary<ulong, bool>();
        private readonly HashSet<int> persistentOwnedEnemies = new HashSet<int>();
        private readonly Dictionary<int, float> persistentActivatedEnemies =
            new Dictionary<int, float>();
        private readonly List<EnemyAI> persistentEnemies = new List<EnemyAI>();
        private int persistentLureClientId = -1;
        private float nextPersistentEnemyRefresh;
        private readonly List<EnemyType> enemyCatalog = new List<EnemyType>();
        private readonly List<Item> itemCatalog = new List<Item>();
        private readonly List<SelectableLevel> moonCatalog = new List<SelectableLevel>();
        private readonly List<int> moonLevelIndices = new List<int>();
        private StartOfRound catalogRound;
        private bool enemyCatalogReady;
        private bool itemCatalogReady;
        private bool moonCatalogReady;
        private float nextCatalogRescan;
        private int enemyCatalogSignature;
        private int itemCatalogSignature;
        private int moonCatalogSignature;
        private float nextLureNoise;
        private bool localKillPending;
        private bool flyEnabled;
        private PlayerControllerB flyPlayer;
        private CharacterController flyController;
        private float flySpeed = 15f;
        private const float FlapCycleSpeed = 11f;
        private const float FlapClimbCycleSpeed = 16f;
        private const float FlapAmplitudeDegrees = 85f;
        private const float FlapLiftDegrees = 25f;
        private float flapPhase;
        private readonly List<Transform> flapBones = new List<Transform>();
        private readonly List<float> flapBoneSides = new List<float>();
        private readonly List<Quaternion> flapBaseRotations = new List<Quaternion>();
        private readonly List<Quaternion> flapAppliedRotations = new List<Quaternion>();
        private PlayerControllerB flapPlayer;
        private readonly HashSet<int> hivelessBees = new HashSet<int>();
        private bool thirdPersonEnabled;
        private PlayerControllerB thirdPersonPlayer;
        private Camera thirdPersonCamera;
        private Vector3 thirdPersonAppliedOffset;
        private bool thirdPersonOffsetApplied;
        private const float BoneOutlineRadius = 0.14f;
        private const float ThirdPersonDistance = 4.5f;
        private const float ThirdPersonHeight = 1.1f;
        private const float ThirdPersonSide = 0.7f;
        private bool shipHornEnabled;
        private bool carHornEnabled;
        private bool terminalSpamEnabled;
        private bool depositSpamEnabled;
        private bool minesEnabled = true;
        private bool turretsEnabled = true;
        private bool turretsBerserk;
        private bool openShipDoorSpace;
        private bool shotgunSpamEnabled;
        private bool explodeJetpacksOnGrab;
        private bool slideTaunt;
        private bool pjManSpam;
        private float pjManSpamInterval = 0.5f;
        private float nextPjManPulse;
        private float nextTrollPulse;
        private float nextFramePacingRefresh;
        private int capturedVSyncCount;
        private int capturedTargetFrameRate;
        private static readonly MethodInfo KillPlayerServerRpcMethod =
            typeof(PlayerControllerB)
                .GetMethod("KillPlayerServerRpc", BindingFlags.Instance | BindingFlags.NonPublic,
                           null,
                           new Type[] { typeof(int), typeof(bool), typeof(Vector3), typeof(int),
                                        typeof(int), typeof(Vector3), typeof(bool) },
                           null);
        private static readonly Color[] ItemTierColors = {
            new Color(0.38f, 0.42f, 0.47f, 0.24f), new Color(0.5f, 0.5f, 0.5f, 0.24f),
            new Color(10f / 255f, 187f / 255f, 10f / 255f, 0.24f), new Color(1f, 0f, 1f, 0.24f),
            new Color(1f, 165f / 255f, 0f, 0.24f)
        };
        private static readonly Comparison<Vector2> ScreenPointComparison = CompareScreenPoints;
        private static readonly Dictionary<MethodCacheKey, MethodInfo> MethodCache =
            new Dictionary<MethodCacheKey, MethodInfo>();
        private static readonly Dictionary<MemberCacheKey, MemberAccessor> MemberCache =
            new Dictionary<MemberCacheKey, MemberAccessor>();
        private static readonly Dictionary<string, Type> GameTypeCache =
            new Dictionary<string, Type>();
        private static readonly Dictionary<string, ObjectCacheEntry> GameObjectCache =
            new Dictionary<string, ObjectCacheEntry>();
        private static readonly UnityEngine.Object[] NoObjects = new UnityEngine.Object[0];
        private static readonly object[] KillRpcArguments = {
            0, true, Vector3.zero, (int)CauseOfDeath.Bludgeoning, 0, Vector3.zero, false
        };
        [ThreadStatic]
        private static object[] oneArgument;
        [ThreadStatic]
        private static object[] twoArguments;
        private bool Flag(int bit)
        {
            return (espFlags & (1 << bit)) != 0;
        }

        private void Awake()
        {
            capturedVSyncCount = QualitySettings.vSyncCount;
            capturedTargetFrameRate = Application.targetFrameRate;
            QualitySettings.vSyncCount = 0;
            Application.targetFrameRate = -1;
            lineTexture = new Texture2D(1, 1, TextureFormat.RGBA32, false);
            lineTexture.SetPixel(0, 0, Color.white);
            lineTexture.Apply();
            lineTexture.hideFlags = HideFlags.HideAndDontSave;
            DontDestroyOnLoad(gameObject);
            Debug.Log("[Kue] Internal HUD Awake");
            Debug.Log("[Kue] Frame pacing set to uncapped (vSync=0, targetFrameRate=-1)");
        }

        private void OnEnable()
        {
            RenderPipelineManager.beginCameraRendering += OnBeginCameraRendering;
            RenderPipelineManager.endCameraRendering += OnEndCameraRendering;
        }

        private void OnDisable()
        {
            RenderPipelineManager.beginCameraRendering -= OnBeginCameraRendering;
            RenderPipelineManager.endCameraRendering -= OnEndCameraRendering;
            RestoreThirdPersonCamera();
            SetThirdPerson(false);
            RestoreFlapBones();
            ReleaseActiveInputState();
        }

        private void OnDestroy()
        {
            ReleaseActiveInputState();
            NativeBridge.ResetRuntimeCatalogs();
            if (menuTexture != null)
                Destroy(menuTexture);
            uploadedMenuPixelRevision = 0;
            if (lineTexture != null)
                Destroy(lineTexture);
            if (highlightVolume != null)
                Destroy(highlightVolume);
            foreach (KeyValuePair<Color, Material> pair in highlightMaterials)
                if (pair.Value != null)
                    Destroy(pair.Value);
            highlightMaterials.Clear();
            if (highlightStampMaterial != null)
                Destroy(highlightStampMaterial);
            if (highlightCutoutMaterial != null)
                Destroy(highlightCutoutMaterial);
            if (highlightRingMaterial != null)
                Destroy(highlightRingMaterial);
            if (highlightQuad != null)
                Destroy(highlightQuad);
            ReleaseHighlightBuffers();
            QualitySettings.vSyncCount = capturedVSyncCount;
            Application.targetFrameRate = capturedTargetFrameRate;
        }

        private void ReleaseActiveInputState()
        {
            menuOpen = false;
            ReleaseMenu();
            if (flyEnabled || flyPlayer != null || flyController != null)
                SetFly(false);
        }

        private void Update()
        {
            RestoreThirdPersonCamera();
            int configuredMenuKey;
            int configuredTickIntervalMs;
            int revision;
            NativeBridge.GetHudConfig(out espFlags, out maxDistance, out configuredMenuKey,
                                      out configuredTickIntervalMs, out revision);
            menuKey = (Key)configuredMenuKey;
            tickIntervalMs = configuredTickIntervalMs;
            Keyboard keyboard = Keyboard.current;
            if (keyboard != null && keyboard[menuKey].wasPressedThisFrame)
            {
                menuOpen = !menuOpen;
                if (menuOpen)
                    CaptureMenu();
                else
                    ReleaseMenu();
            }
            if (menuOpen && keyboard != null && keyboard.escapeKey.wasPressedThisFrame)
            {
                menuOpen = false;
                ReleaseMenu();
            }

            if (menuOpen)
                CaptureMenu();

            if (Time.unscaledTime >= nextFramePacingRefresh)
            {
                nextFramePacingRefresh = Time.unscaledTime + 1f;
                if (QualitySettings.vSyncCount != 0)
                    QualitySettings.vSyncCount = 0;
                if (Application.targetFrameRate != -1)
                    Application.targetFrameRate = -1;
            }

            PlayerControllerB localPlayer = LocalPlayer();
            int hostState = localPlayer != null && localPlayer.IsHost ? 1 : 0;
            if (hostState != reportedHostState)
            {
                reportedHostState = hostState;
                NativeBridge.ReportHostState(hostState);
            }
            if (revision != configRevision)
            {
                configRevision = revision;
                UpdateOutlineColors();
            }
            RefreshCatalogLifecycle();
            RescanCatalogs();
            RefreshEnemyCatalog();
            RefreshItemCatalog();
            RefreshMoonCatalog();
            ProcessPendingLocalKill();
            ProcessManagedActions();
            UpdatePlayerUtilities();
            UpdateFly();
            UpdateTrollToggles();
            UpdateDeathNotifications();
            if (Time.unscaledTime >= nextScan)
            {
                nextScan = Time.unscaledTime + 0.5f;
                RebuildMarks();
            }
        }

        private void LateUpdate()
        {
            ApplyLocalPlayerFeatures();
            UpdatePersistentLure();
            UpdateFlappingArms();
            UpdateThirdPersonModel();
            CollectHighlightDraws();
        }

        private bool EnsureHighlightPass()
        {
            if (highlightVolume != null)
                return true;
            if (highlightUnavailable)
                return false;
            try
            {
                highlightShader = Shader.Find("UI/Default");
                if (highlightShader == null)
                    highlightShader = Shader.Find("Sprites/Default");
                highlightCutoutShader = Shader.Find("Hidden/Internal-Colored");
                if (highlightShader == null || highlightCutoutShader == null)
                {
                    highlightUnavailable = true;
                    Debug.Log("[Kue] Built-in outline shaders unavailable; using hull outlines");
                    return false;
                }
                highlightVolume = gameObject.AddComponent<CustomPassVolume>();
                highlightVolume.isGlobal = true;
                highlightVolume.injectionPoint = CustomPassInjectionPoint.AfterPostProcess;
                highlightVolume.customPasses.Add(new ModelHighlightPass { owner = this });
                Debug.Log("[Kue] Model highlight pass installed with " + highlightShader.name);
                return true;
            }
            catch (Exception e)
            {
                DisableHighlightPass("[Kue] Model highlight pass setup failed: " + e);
                return false;
            }
        }

        private void DisableHighlightPass(string reason)
        {
            highlightUnavailable = true;
            highlightDraws.Clear();
            if (highlightVolume != null)
            {
                Destroy(highlightVolume);
                highlightVolume = null;
            }
            Debug.LogError(reason);
        }

        private Material HighlightQuadMaterial(Texture texture, Color color)
        {
            Material material = new Material(highlightShader) { hideFlags = HideFlags.HideAndDontSave };
            material.SetColor("_Color", color);
            if (texture != null)
                material.SetTexture("_MainTex", texture);
            return material;
        }

        private Material HighlightMaterial(Color color)
        {
            Material material;
            if (highlightMaterials.TryGetValue(color, out material) && material != null)
                return material;
            material = HighlightQuadMaterial(null, new Color(color.r, color.g, color.b, 1f));
            highlightMaterials[color] = material;
            return material;
        }

        private void ReleaseHighlightBuffers()
        {
            if (highlightMask != null)
                RTHandles.Release(highlightMask);
            if (highlightRing != null)
                RTHandles.Release(highlightRing);
            highlightMask = null;
            highlightRing = null;
            highlightWidth = 0;
            highlightHeight = 0;
        }

        private static RTHandle AllocateHighlightBuffer(int width, int height, string name)
        {
            return RTHandles.Alloc(width, height, colorFormat: GraphicsFormat.R8G8B8A8_UNorm,
                                   filterMode: FilterMode.Point, wrapMode: TextureWrapMode.Clamp,
                                   name: name);
        }

        private bool EnsureHighlightResources(int width, int height)
        {
            if (width <= 0 || height <= 0)
                return false;
            if (highlightQuad == null)
            {
                highlightQuad = new Mesh { hideFlags = HideFlags.HideAndDontSave };
                highlightQuad.vertices = new Vector3[4];
                highlightQuad.uv = new[] { new Vector2(0f, 0f), new Vector2(1f, 0f),
                                           new Vector2(1f, 1f), new Vector2(0f, 1f) };
                highlightQuad.colors = new[] { Color.white, Color.white, Color.white, Color.white };
                highlightQuad.triangles = new[] { 0, 2, 1, 0, 3, 2, 0, 1, 2, 0, 2, 3 };
            }
            if (highlightMask != null && highlightWidth == width && highlightHeight == height)
                return true;
            ReleaseHighlightBuffers();
            highlightMask = AllocateHighlightBuffer(width, height, "KueHighlightMask");
            highlightRing = AllocateHighlightBuffer(width, height, "KueHighlightRing");
            highlightWidth = width;
            highlightHeight = height;
            if (highlightStampMaterial != null)
                Destroy(highlightStampMaterial);
            if (highlightRingMaterial != null)
                Destroy(highlightRingMaterial);
            highlightStampMaterial = HighlightQuadMaterial(highlightMask.rt, Color.white);
            highlightRingMaterial = HighlightQuadMaterial(highlightRing.rt, Color.white);
            if (highlightCutoutMaterial == null)
            {
                highlightCutoutMaterial =
                    new Material(highlightCutoutShader) { hideFlags = HideFlags.HideAndDontSave };
                highlightCutoutMaterial.SetColor("_Color", Color.clear);
                highlightCutoutMaterial.SetInt("_SrcBlend", (int)BlendMode.Zero);
                highlightCutoutMaterial.SetInt("_DstBlend", (int)BlendMode.Zero);
                highlightCutoutMaterial.SetInt("_ZWrite", 0);
                highlightCutoutMaterial.SetInt("_ZTest", (int)CompareFunction.Always);
                highlightCutoutMaterial.SetInt("_Cull", (int)CullMode.Off);
            }
            return true;
        }

        private Matrix4x4 HighlightProjection(Camera camera)
        {
            Matrix4x4 projection = camera.projectionMatrix;
            float near = camera.nearClipPlane;
            float far = Mathf.Max(camera.farClipPlane, maxDistance * 2f);
            if (camera.orthographic || far <= camera.farClipPlane || near <= 0f)
                return projection;
            projection.m22 = -(far + near) / (far - near);
            projection.m23 = -2f * far * near / (far - near);
            return projection;
        }

        private void UpdateHighlightQuad(Camera camera)
        {
            float depth = camera.nearClipPlane * 2f + 0.05f;
            highlightQuadVertices.Clear();
            highlightQuadVertices.Add(camera.ViewportToWorldPoint(new Vector3(0f, 0f, depth)));
            highlightQuadVertices.Add(camera.ViewportToWorldPoint(new Vector3(1f, 0f, depth)));
            highlightQuadVertices.Add(camera.ViewportToWorldPoint(new Vector3(1f, 1f, depth)));
            highlightQuadVertices.Add(camera.ViewportToWorldPoint(new Vector3(0f, 1f, depth)));
            highlightQuad.SetVertices(highlightQuadVertices);
            highlightQuad.RecalculateBounds();
            highlightPixelRight =
                (highlightQuadVertices[1] - highlightQuadVertices[0]) / highlightWidth;
            highlightPixelUp = (highlightQuadVertices[3] - highlightQuadVertices[0]) / highlightHeight;
        }

        private void StampHighlightRing(CommandBuffer cmd)
        {
            for (int ring = 1; ring <= 2; ring++)
            {
                float radius = HighlightRingRadiusPixels * ring * 0.5f;
                for (int tap = 0; tap < HighlightRingTaps; tap++)
                {
                    float angle = tap * (Mathf.PI * 2f / HighlightRingTaps);
                    Vector3 offset = highlightPixelRight * (Mathf.Cos(angle) * radius) +
                                     highlightPixelUp * (Mathf.Sin(angle) * radius);
                    cmd.DrawMesh(highlightQuad, Matrix4x4.Translate(offset), highlightStampMaterial,
                                 0, 0);
                }
            }
        }

        private void DrawHighlightModels(CommandBuffer cmd, Material overrideMaterial)
        {
            for (int i = 0; i < highlightDraws.Count; i++)
            {
                HighlightDraw draw = highlightDraws[i];
                Material material = overrideMaterial != null ? overrideMaterial : draw.material;
                if (draw.renderer == null || material == null)
                    continue;
                for (int submesh = 0; submesh < draw.submeshCount; submesh++)
                    cmd.DrawRenderer(draw.renderer, material, submesh, 0);
            }
        }

        private static int SubmeshCount(Renderer renderer)
        {
            SkinnedMeshRenderer skinned = renderer as SkinnedMeshRenderer;
            if (skinned != null)
                return skinned.sharedMesh != null ? skinned.sharedMesh.subMeshCount : 0;
            MeshFilter filter = renderer.GetComponent<MeshFilter>();
            return filter != null && filter.sharedMesh != null ? filter.sharedMesh.subMeshCount : 0;
        }

        private void CollectHighlightDraws()
        {
            highlightDraws.Clear();
            if ((espFlags & 0x3f) == 0 || !Flag(9) || !EnsureHighlightPass())
                return;
            PlayerControllerB local = LocalPlayer();
            Camera camera = GameCamera();
            if (camera == null)
                return;
            Vector3 origin = local != null ? local.transform.position : camera.transform.position;
            float maximumDistanceSquared = maxDistance * maxDistance;
            for (int markIndex = 0; markIndex < markCount; markIndex++)
            {
                Mark mark = marks[markIndex];
                if (!mark.enabled || mark.marker == null || mark.portal || mark.renderers == null ||
                    !MarkStillVisible(mark))
                    continue;
                Vector3 markerPosition = mark.marker.position;
                if ((origin - markerPosition).sqrMagnitude > maximumDistanceSquared)
                    continue;
                Color drawColor = mark.color;
                drawColor.a = 1f;
                Material material = HighlightMaterial(drawColor);
                float radiusSquared = mark.rendererRadius * mark.rendererRadius;
                bool limitRadius = mark.kind != MarkKind.Player && mark.rendererRadius > 0f;
                int rendererLimit = mark.kind == MarkKind.Player ? 1 : mark.renderers.Length;
                for (int i = 0; i < mark.renderers.Length && i < rendererLimit; i++)
                {
                    Renderer renderer = mark.renderers[i];
                    if (renderer == null || !renderer.enabled ||
                        (!(renderer is MeshRenderer) && !(renderer is SkinnedMeshRenderer)))
                    {
                        if (mark.kind == MarkKind.Player)
                            rendererLimit++;
                        continue;
                    }
                    if (limitRadius)
                    {
                        Bounds bounds = renderer.bounds;
                        if (bounds.extents.sqrMagnitude > radiusSquared ||
                            (bounds.center - markerPosition).sqrMagnitude > radiusSquared)
                            continue;
                    }
                    int submeshCount = SubmeshCount(renderer);
                    if (submeshCount <= 0)
                        continue;
                    SkinnedMeshRenderer skinned = renderer as SkinnedMeshRenderer;
                    if (skinned != null && !skinned.updateWhenOffscreen)
                        skinned.updateWhenOffscreen = true;
                    highlightDraws.Add(new HighlightDraw { renderer = renderer, material = material,
                                                           submeshCount = submeshCount });
                }
            }
        }

        private void ExecuteModelHighlight(CustomPassContext context)
        {
            if (highlightDraws.Count == 0 || context.hdCamera == null ||
                context.hdCamera.camera != activeCamera)
                return;
            try
            {
                if (!EnsureHighlightResources(context.hdCamera.actualWidth,
                                              context.hdCamera.actualHeight))
                    return;
                CommandBuffer cmd = context.cmd;
                Camera camera = context.hdCamera.camera;
                UpdateHighlightQuad(camera);
                cmd.SetViewProjectionMatrices(camera.worldToCameraMatrix,
                                              HighlightProjection(camera));
                CoreUtils.SetRenderTarget(cmd, highlightMask, ClearFlag.Color, Color.clear);
                DrawHighlightModels(cmd, null);
                CoreUtils.SetRenderTarget(cmd, highlightRing, ClearFlag.Color, Color.clear);
                StampHighlightRing(cmd);
                DrawHighlightModels(cmd, highlightCutoutMaterial);
                CoreUtils.SetRenderTarget(cmd, context.cameraColorBuffer);
                cmd.DrawMesh(highlightQuad, Matrix4x4.identity, highlightRingMaterial, 0, 0);
                cmd.SetViewProjectionMatrices(camera.worldToCameraMatrix, camera.projectionMatrix);
                if (!highlightResourcesLogged)
                {
                    highlightResourcesLogged = true;
                    Debug.Log("[Kue] Model highlight ring: shader=" + highlightShader.name +
                              " size=" + highlightWidth + "x" + highlightHeight + " draws=" +
                              highlightDraws.Count + " quad0=" +
                              highlightQuadVertices[0].ToString("F2") + " cam=" +
                              context.hdCamera.camera.transform.position.ToString("F2"));
                }
            }
            catch (Exception e)
            {
                DisableHighlightPass("[Kue] Model highlight draw failed: " + e);
            }
        }

        private void ApplyLocalPlayerFeatures()
        {
            if (Time.unscaledTime < nextPlayerFeatureWrite)
                return;
            nextPlayerFeatureWrite = Time.unscaledTime + tickIntervalMs / 1000f;
            PlayerControllerB local = LocalPlayer();
            if (local == null)
                return;
            if (Flag(15))
            {
                local.sprintMeter = 1f;
                local.isExhausted = false;
            }
            if (Flag(16))
                local.carryWeight = 1f;
        }

        private void RenderImGuiMenu()
        {
            Mouse mouse = Mouse.current;
            Vector2 mousePos = mouse != null ? mouse.position.ReadValue() : Vector2.zero;
            float mx = mousePos.x;
            float my = Screen.height - mousePos.y;
            int down = mouse != null && mouse.leftButton.isPressed ? 1 : 0;
            float wheel = mouse != null ? mouse.scroll.ReadValue().y / 120f : 0f;
            int x, y, width, height, bytes;
            ulong pixelRevision;
            IntPtr pixels =
                NativeBridge.RenderMenu(Screen.width, Screen.height, mx, my, down, wheel, out x,
                                        out y, out width, out height, out bytes, out pixelRevision);
            if (pixels == IntPtr.Zero || width <= 0 || height <= 0 || bytes == 0)
                return;
            long expectedBytes = (long)width * height * 4L;
            long suppliedBytes = bytes < 0 ? -(long)bytes : bytes;
            if (x < 0 || y < 0 || width > Screen.width || height > Screen.height ||
                (long)x + width > Screen.width || (long)y + height > Screen.height ||
                expectedBytes > int.MaxValue || suppliedBytes != expectedBytes ||
                pixelRevision == 0)
            {
                if (!menuBufferFailureReported)
                {
                    menuBufferFailureReported = true;
                    ReportActionFailure("Menu render buffer failed validation");
                }
                return;
            }
            menuBufferFailureReported = false;
            menuRect = new Rect(x, y, width, height);

            if (menuTexture == null || menuTexture.width != width || menuTexture.height != height)
            {
                if (menuTexture != null)
                    Destroy(menuTexture);
                menuTexture = new Texture2D(width, height, TextureFormat.RGBA32, false, false);
                menuTexture.wrapMode = TextureWrapMode.Clamp;
                menuTexture.filterMode = FilterMode.Bilinear;
                menuTexture.hideFlags = HideFlags.HideAndDontSave;
                uploadedMenuPixelRevision = 0;
            }
            if (uploadedMenuPixelRevision == pixelRevision)
                return;
            menuTexture.LoadRawTextureData(pixels, (int)suppliedBytes);
            menuTexture.Apply(false, false);
            uploadedMenuPixelRevision = pixelRevision;
        }

        private void CaptureMenu()
        {
            if (!menuCaptureActive)
            {
                capturedCursorLockState = Cursor.lockState;
                capturedCursorVisible = Cursor.visible;
                menuCaptureActive = true;
            }
            PlayerControllerB local = LocalPlayer();
            if (menuCapturedPlayer != local)
            {
                RestoreCapturedPlayerInput();
                menuCapturedPlayer = local;
                if (local != null)
                {
                    capturedMoveDisabled = local.disableMoveInput;
                    capturedLookDisabled = local.disableLookInput;
                }
            }
            Cursor.lockState = CursorLockMode.None;
            Cursor.visible = true;
            if (local == null)
                return;
            local.disableMoveInput = true;
            local.disableLookInput = true;
        }

        private void ReleaseMenu()
        {
            if (!menuCaptureActive)
                return;
            RestoreCapturedPlayerInput();
            Cursor.lockState = capturedCursorLockState;
            Cursor.visible = capturedCursorVisible;
            menuCaptureActive = false;
        }

        private void RestoreCapturedPlayerInput()
        {
            if (menuCapturedPlayer != null)
            {
                menuCapturedPlayer.disableMoveInput = capturedMoveDisabled;
                menuCapturedPlayer.disableLookInput = capturedLookDisabled;
            }
            menuCapturedPlayer = null;
        }

        private static PlayerControllerB LocalPlayer()
        {
            return GameNetworkManager.Instance == null
                       ? null
                       : GameNetworkManager.Instance.localPlayerController;
        }

        private static bool RealPlayer(PlayerControllerB player)
        {
            if (player == null)
                return false;
            PlayerControllerB local = LocalPlayer();
            if (player == local)
                return true;
            if (player.playerSteamId == 0 || !player.isPlayerControlled)
                return false;
            if (local != null && player.playerSteamId == local.playerSteamId)
                return false;
            StartOfRound round = StartOfRound.Instance;
            if (round != null && round.notSpawnedPosition != null &&
                Vector3.Distance(player.serverPlayerPosition, round.notSpawnedPosition.position) <=
                    10f)
                return false;
            return true;
        }

        private static PlayerControllerB PlayerByClientId(int clientId)
        {
            StartOfRound round = StartOfRound.Instance;
            if (round == null || round.allPlayerScripts == null)
                return null;

            foreach (PlayerControllerB player in round.allPlayerScripts)
            {
                if (player == null || (int)player.playerClientId != clientId)
                    continue;
                if (player == LocalPlayer())
                    return player;
                if (!player.disconnectedMidGame &&
                    (player.isPlayerControlled || player.playerSteamId != 0))
                    return player;
            }
            return null;
        }

        private static Vector3 TargetPosition(PlayerControllerB target)
        {
            if (target == null)
                return Vector3.zero;

            if (target != LocalPlayer())
            {
                Vector3 server = target.serverPlayerPosition;
                if (!float.IsNaN(server.x) && !float.IsNaN(server.y) && !float.IsNaN(server.z) &&
                    !float.IsInfinity(server.x) && !float.IsInfinity(server.y) &&
                    !float.IsInfinity(server.z))
                    return server;
            }
            return target.transform.position;
        }

        private static Vector3 TargetForward(PlayerControllerB target)
        {
            if (target == null || target.transform == null)
                return Vector3.forward;
            Vector3 forward = Vector3.ProjectOnPlane(target.transform.forward, Vector3.up);
            return forward.sqrMagnitude > 0.01f ? forward.normalized : Vector3.forward;
        }

        private static bool Finite(float value)
        {
            return !float.IsNaN(value) && !float.IsInfinity(value);
        }

        private static bool Finite(Vector2 value)
        {
            return Finite(value.x) && Finite(value.y);
        }

        private void KillPlayerManaged(PlayerControllerB player)
        {
            if (player == null)
            {
                ReportActionFailure("Kill failed: target unavailable");
                return;
            }
            if (player.isPlayerDead)
                return;
            PlayerControllerB local = LocalPlayer();
            if (local == null)
            {
                ReportActionFailure("Kill failed: local player unavailable");
                return;
            }
            if (player == local)
            {
                QueueLocalKill(local);
                return;
            }

            if (KillPlayerServerRpcMethod == null)
                throw new MissingMethodException("PlayerControllerB.KillPlayerServerRpc");
            KillRpcArguments[0] = (int)player.playerClientId;
            KillPlayerServerRpcMethod.Invoke(local, KillRpcArguments);
            Debug.Log("[Kue] Remote KillPlayerServerRpc sent for " + player.playerUsername + " (" +
                      player.playerClientId + ")");
        }

        private void QueueLocalKill(PlayerControllerB local)
        {
            if (localKillPending || (local != null && local.isPlayerDead))
                return;
            if (local == null)
            {
                ReportActionFailure("Local kill failed: local player unavailable");
                return;
            }
            localKillPending = true;
            Debug.Log("[Kue] Local death queued for next Unity frame");
        }

        private static void ReportActionFailure(string message)
        {
            Debug.LogError("[Kue] " + message);
            HUDManager hud = HUDManager.Instance;
            if (hud != null)
                hud.DisplayTip("Kue", message);
        }

        private int LureEnemiesManaged(PlayerControllerB player, bool refreshOwnership)
        {
            PlayerControllerB local = LocalPlayer();
            if (!RealPlayer(player) || local == null)
                return 0;
            EnemyAI[] loadedEnemies = UnityEngine.Object.FindObjectsOfType<EnemyAI>();
            List<EnemyAI> enemies = new List<EnemyAI>(loadedEnemies.Length);
            int count = 0;
            foreach (EnemyAI enemy in loadedEnemies)
            {
                if (enemy == null || enemy.isEnemyDead)
                    continue;
                try
                {
                    int instanceId = enemy.GetInstanceID();
                    if (refreshOwnership && persistentOwnedEnemies.Add(instanceId))
                        enemy.ChangeEnemyOwnerServerRpc(local.actualClientId);
                    enemies.Add(enemy);
                    count++;
                }
                catch (Exception e)
                {
                    Debug.LogError("[Kue] Enemy lure failed for " + enemy.name + ": " + e);
                }
            }
            StartCoroutine(ApplyLureAfterOwnership(enemies, player, 90));
            return count;
        }

        private static bool EnemyOwnedBy(EnemyAI enemy, PlayerControllerB local)
        {
            return enemy != null && local != null && enemy.thisNetworkObject != null &&
                   enemy.thisNetworkObject.OwnerClientId == local.actualClientId;
        }

        private void ForceEnemyTarget(EnemyAI enemy, PlayerControllerB target,
                                      bool refreshNetworkState, bool noise = false)
        {
            if (enemy == null || target == null || enemy.isEnemyDead)
                return;
            enemy.targetPlayer = target;
            enemy.SetMovingTowardsTargetPlayer(target);
            enemy.destination = SnapToNavMesh(target.transform.position);
            enemy.moveTowardsDestination = true;
            if (noise)
            {
                try
                {
                    enemy.DetectNoise(target.transform.position, 1f, 0, 0);
                }
                catch (Exception e)
                {
                    Debug.LogError("[Kue] Lure noise failed for " + enemy.name + ": " + e);
                }
            }
            if (!refreshNetworkState)
                return;

            string type = enemy.GetType().Name;
            int chase = 1;
            int aggravated = 2;

            if (type == "CrawlerAI")
                InvokeAny(enemy, "BeginChasingPlayerServerRpc", (int)target.playerClientId);
            else if (type == "MouthDogAI")
                InvokeAny(enemy, "ReactToOtherDogHowl", TargetPosition(target));
            else if (type == "BaboonBirdAI")
                InvokeAny(enemy, "SetAggressiveModeServerRpc", 1);
            else if (type == "ForestGiantAI")
            {
                InvokeAny(enemy, "SwitchToBehaviourServerRpc", chase);
                SetMember(enemy, "chasingPlayer", target);
                SetMember(enemy, "investigating", true);
                SetMember(enemy, "lostPlayerInChase", false);
                InvokeAny(enemy, "SetDestinationToPosition", TargetPosition(target), false);
            }
            else if (type == "FlowermanAI")
            {
                InvokeAny(enemy, "SwitchToBehaviourServerRpc", aggravated);
                InvokeAny(enemy, "EnterAngerModeServerRpc", 20);
            }
            else if (type == "HoarderBugAI")
            {
                InvokeAny(enemy, "SwitchToBehaviourServerRpc", aggravated);
                SetMember(enemy, "angryAtPlayer", target);
                SetMember(enemy, "angryTimer", float.MaxValue);
                SetMember(enemy, "lostPlayerInChase", false);
                InvokeAny(enemy, "SyncNestPositionServerRpc", TargetPosition(target));
            }
            else if (type == "NutcrackerEnemyAI")
            {
                InvokeAny(enemy, "SwitchToBehaviourServerRpc", aggravated);
                SetMember(enemy, "lastSeenPlayerPos", TargetPosition(target));
                SetMember(enemy, "timeSinceSeeingTarget", 0f);
            }
            else if (type == "BushWolfEnemy")
            {
                SetMember(enemy, "isHiding", false);
                SetMember(enemy, "staringAtPlayer", target);
                InvokeAny(enemy, "SwitchToBehaviourServerRpc", chase);
            }
            else if (type == "DressGirlAI")
            {
                SetMember(enemy, "hauntingPlayer", target);
                InvokeAny(enemy, "SwitchToBehaviourServerRpc", chase);
            }
            else if (type == "ButlerEnemyAI")
                InvokeAny(enemy, "SwitchToBehaviourServerRpc", aggravated);
            else if (type == "RadMechAI")
            {
                InvokeAny(enemy, "SetChargingForwardClientRpc", true);
                InvokeAny(enemy, "SwitchToBehaviourServerRpc", chase);
            }
            else if (type == "CentipedeAI" || type == "SandSpiderAI" || type == "RedLocustBees" ||
                     type == "PufferAI" || type == "JesterAI")
                InvokeAny(enemy, "SwitchToBehaviourServerRpc", aggravated);
            else
                InvokeAny(enemy, "SwitchToBehaviourServerRpc", chase);
        }

        private IEnumerator ApplyLureAfterOwnership(List<EnemyAI> enemies, PlayerControllerB target,
                                                    int frames)
        {
            PlayerControllerB local = LocalPlayer();
            if (local == null || target == null)
                yield break;
            HashSet<int> activated = new HashSet<int>();
            for (int frame = 0; frame < frames; frame++)
            {
                foreach (EnemyAI enemy in enemies)
                {
                    if (enemy == null || enemy.isEnemyDead)
                        continue;
                    int id = enemy.GetInstanceID();
                    bool owned = EnemyOwnedBy(enemy, local);
                    if (!owned && frame % 15 == 0)
                    {
                        try
                        {
                            enemy.ChangeEnemyOwnerServerRpc(local.actualClientId);
                        }
                        catch (Exception e)
                        {
                            Debug.LogError("[Kue] Lure ownership retry failed: " + e);
                        }
                    }
                    if (owned)
                    {
                        ForceEnemyTarget(enemy, target, activated.Add(id));
                    }
                }
                yield return null;
            }
            Debug.Log("[Kue] Lure activated " + activated.Count + "/" + enemies.Count +
                      " enemies for " + target.playerUsername + " as host=" + local.IsHost);
            if (HUDManager.Instance != null)
                HUDManager.Instance.DisplayTip("Kue", "Lured " + activated.Count + " enemies to " +
                                                          target.playerUsername);
        }

        private void UpdatePersistentLure()
        {
            if (persistentLureClientId < 0)
                return;
            PlayerControllerB local = LocalPlayer();
            PlayerControllerB target = PlayerByClientId(persistentLureClientId);
            if (local == null || target == null)
            {
                persistentLureClientId = -1;
                persistentOwnedEnemies.Clear();
                persistentActivatedEnemies.Clear();
                persistentEnemies.Clear();
                NativeBridge.ReportPersistentLureTarget(-1);
                return;
            }
            if (target.isPlayerDead)
                return;
            bool noise = Time.unscaledTime >= nextLureNoise;
            if (noise)
                nextLureNoise = Time.unscaledTime + 0.5f;

            if (Time.unscaledTime >= nextPersistentEnemyRefresh)
            {
                nextPersistentEnemyRefresh = Time.unscaledTime + 0.25f;
                persistentEnemies.Clear();
                foreach (UnityEngine.Object loaded in FindAll("EnemyAI"))
                {
                    EnemyAI enemy = loaded as EnemyAI;
                    if (enemy == null || enemy.isEnemyDead)
                        continue;
                    persistentEnemies.Add(enemy);
                    int instanceId = enemy.GetInstanceID();
                    if (persistentOwnedEnemies.Add(instanceId))
                    {
                        try
                        {
                            enemy.ChangeEnemyOwnerServerRpc(local.actualClientId);
                        }
                        catch (Exception e)
                        {
                            Debug.LogError("[Kue] Enemy ownership transfer failed: " + e);
                        }
                    }
                    else if (!EnemyOwnedBy(enemy, local))
                    {
                        persistentActivatedEnemies.Remove(instanceId);
                        try
                        {
                            enemy.ChangeEnemyOwnerServerRpc(local.actualClientId);
                        }
                        catch (Exception e)
                        {
                            Debug.LogError("[Kue] Persistent ownership refresh failed: " + e);
                        }
                    }
                    if (EnemyOwnedBy(enemy, local))
                    {
                        float activatedAt;
                        bool refresh =
                            !persistentActivatedEnemies.TryGetValue(instanceId, out activatedAt) ||
                            Time.unscaledTime - activatedAt >= 1.5f;
                        if (refresh)
                            persistentActivatedEnemies[instanceId] = Time.unscaledTime;
                        ForceEnemyTarget(enemy, target, refresh, noise);
                    }
                }
                return;
            }
            foreach (EnemyAI enemy in persistentEnemies)
            {
                if (enemy == null || enemy.isEnemyDead)
                    continue;
                if (EnemyOwnedBy(enemy, local))
                    ForceEnemyTarget(enemy, target, false, noise);
            }
        }

        private void ProcessPendingLocalKill()
        {
            if (!localKillPending)
                return;
            localKillPending = false;

            PlayerControllerB local = LocalPlayer();
            if (local == null)
            {
                ReportActionFailure("Local kill failed: local player unavailable");
                return;
            }
            if (local.isPlayerDead)
                return;

            StartOfRound round = StartOfRound.Instance;
            StartOfRound playerRound = local.playersManager;
            if (round == null || playerRound == null)
            {
                ReportActionFailure("Local kill failed: round state unavailable");
                return;
            }

            round.allowLocalPlayerDeath = true;
            if (round.timeSinceRoundStarted < MinimumLocalDeathRoundTime)
                round.timeSinceRoundStarted = MinimumLocalDeathRoundTime;
            playerRound.shipDoorsEnabled = true;
            if (!local.AllowPlayerDeath())
            {
                ReportActionFailure("Local kill failed: the game rejected player death");
                return;
            }

            if (menuOpen)
            {
                menuOpen = false;
                ReleaseMenu();
            }
            local.KillPlayer(Vector3.zero, true, CauseOfDeath.Bludgeoning, 0, Vector3.zero, false);
            if (!local.isPlayerDead)
            {
                ReportActionFailure("Local kill failed: the player remained alive");
                return;
            }
            Debug.Log("[Kue] Local death executed directly");
        }

        private void KillHivelessBees(EnemyAI enemy, PlayerControllerB local)
        {
            RedLocustBees bees = enemy as RedLocustBees;
            if (bees == null || local == null || !local.IsHost)
                return;
            int instanceId = bees.GetInstanceID();
            if (bees.hive != null)
            {
                hivelessBees.Remove(instanceId);
                return;
            }
            if (hivelessBees.Add(instanceId))
                return;
            hivelessBees.Remove(instanceId);
            try
            {
                bees.KillEnemyOnOwnerClient(true);
                Debug.Log("[Kue] Removed " + bees.name +
                          ": its hive is gone and the game errors every frame");
            }
            catch (Exception e)
            {
                Debug.LogError("[Kue] Hiveless bees removal failed: " + e);
            }
        }

        private void RefreshEnemyCatalog()
        {
            if (enemyCatalogReady)
                return;
            EnemyType[] assets = Resources.FindObjectsOfTypeAll<EnemyType>();
            enemyCatalogSignature = assets.Length;
            enemyCatalogBuffer.Clear();
            catalogInstanceIds.Clear();
            foreach (EnemyType type in assets)
            {
                if (type == null || type.enemyPrefab == null ||
                    string.IsNullOrEmpty(type.enemyName) ||
                    !catalogInstanceIds.Add(type.GetInstanceID()))
                    continue;
                enemyCatalogBuffer.Add(type);
            }
            if (enemyCatalogBuffer.Count == 0)
                return;
            enemyCatalogBuffer.Sort(CompareEnemyTypes);
            CatalogBeginResult begin = NativeBridge.BeginEnemyCatalog();
            if (begin != CatalogBeginResult.Begun)
            {
                Debug.LogError("[Kue] Runtime enemy catalog begin failed: " + begin);
                return;
            }
            usedCatalogNames.Clear();
            for (int i = 0; i < enemyCatalogBuffer.Count; i++)
            {
                EnemyType type = enemyCatalogBuffer[i];
                CatalogReportResult report = NativeBridge.ReportEnemyType(
                    type.GetInstanceID(), UniqueCatalogName(type.enemyName));
                if (report != CatalogReportResult.Recorded)
                {
                    NativeBridge.AbortEnemyCatalog();
                    Debug.LogError("[Kue] Runtime enemy catalog report failed at " + i + ": " +
                                   report);
                    return;
                }
            }
            CatalogCommitResult commit = NativeBridge.CommitEnemyCatalog();
            if (commit != CatalogCommitResult.Committed &&
                commit != CatalogCommitResult.Unchanged)
            {
                Debug.LogError("[Kue] Runtime enemy catalog commit failed: " + commit);
                return;
            }
            enemyCatalog.Clear();
            enemyCatalog.AddRange(enemyCatalogBuffer);
            enemyCatalogIndices.Clear();
            enemyCatalogNames.Clear();
            for (int i = 0; i < enemyCatalog.Count; i++)
            {
                enemyCatalogIndices[enemyCatalog[i]] = i;
                enemyCatalogNames[enemyCatalog[i].enemyName] = i;
            }
            activeEnemyCounts = new int[enemyCatalog.Count];
            enemyCatalogReady = true;
            Debug.Log("[Kue] Runtime enemy catalog: " + enemyCatalog.Count + " installed types");
        }

        private void RescanCatalogs()
        {
            if (Time.unscaledTime < nextCatalogRescan)
                return;
            nextCatalogRescan = Time.unscaledTime + 10f;
            if (enemyCatalogReady &&
                Resources.FindObjectsOfTypeAll<EnemyType>().Length != enemyCatalogSignature)
            {
                enemyCatalogReady = false;
                Debug.Log("[Kue] Enemy types changed; rescanning the catalog");
            }
            StartOfRound round = StartOfRound.Instance;
            if (itemCatalogReady && round != null &&
                ItemCatalogSignature(round, Resources.FindObjectsOfTypeAll<Item>().Length) !=
                    itemCatalogSignature)
            {
                itemCatalogReady = false;
                Debug.Log("[Kue] Items changed; rescanning the catalog");
            }
            if (moonCatalogReady && round != null &&
                MoonCatalogSignature(round,
                                     Resources.FindObjectsOfTypeAll<SelectableLevel>().Length) !=
                    moonCatalogSignature)
            {
                moonCatalogReady = false;
                Debug.Log("[Kue] Moons changed; rescanning the catalog");
            }
        }

        private static int MoonCatalogSignature(StartOfRound round, int loadedCount)
        {
            int listed = round.levels != null ? round.levels.Length : 0;
            return listed * 65536 + loadedCount;
        }

        private string UniqueCatalogName(string name)
        {
            string candidate = CatalogName(name);
            for (int copy = 2; !usedCatalogNames.Add(candidate); copy++)
                candidate = CatalogName(name) + " (" + copy + ")";
            return candidate;
        }

        private static string MoonName(SelectableLevel level, bool unlisted)
        {
            string name = string.IsNullOrEmpty(level.PlanetName) ? level.name : level.PlanetName;
            return unlisted ? name + " (unlisted)" : name;
        }

        private void RefreshMoonCatalog()
        {
            if (moonCatalogReady)
                return;
            StartOfRound round = StartOfRound.Instance;
            if (round == null || round.levels == null)
                return;
            SelectableLevel[] loaded = Resources.FindObjectsOfTypeAll<SelectableLevel>();
            moonCatalogSignature = MoonCatalogSignature(round, loaded.Length);
            moonCatalogBuffer.Clear();
            moonLevelBuffer.Clear();
            catalogInstanceIds.Clear();
            for (int i = 0; i < round.levels.Length; i++)
            {
                SelectableLevel level = round.levels[i];
                if (level == null || !catalogInstanceIds.Add(level.GetInstanceID()))
                    continue;
                moonCatalogBuffer.Add(level);
                moonLevelBuffer.Add(i);
            }
            int routable = moonCatalogBuffer.Count;
            foreach (SelectableLevel level in loaded)
            {
                if (level == null || !catalogInstanceIds.Add(level.GetInstanceID()))
                    continue;
                moonCatalogBuffer.Add(level);
                moonLevelBuffer.Add(-1);
            }
            if (moonCatalogBuffer.Count == 0)
                return;
            CatalogBeginResult begin = NativeBridge.BeginMoonCatalog();
            if (begin != CatalogBeginResult.Begun)
            {
                Debug.LogError("[Kue] Runtime moon catalog begin failed: " + begin);
                return;
            }
            usedCatalogNames.Clear();
            for (int i = 0; i < moonCatalogBuffer.Count; i++)
            {
                SelectableLevel level = moonCatalogBuffer[i];
                string name = UniqueCatalogName(MoonName(level, moonLevelBuffer[i] < 0));
                CatalogReportResult report =
                    NativeBridge.ReportMoonType(level.GetInstanceID(), name);
                if (report != CatalogReportResult.Recorded)
                {
                    NativeBridge.AbortMoonCatalog();
                    Debug.LogError("[Kue] Runtime moon catalog report failed at " + i + ": " +
                                   report);
                    return;
                }
            }
            CatalogCommitResult commit = NativeBridge.CommitMoonCatalog();
            if (commit != CatalogCommitResult.Committed &&
                commit != CatalogCommitResult.Unchanged)
            {
                Debug.LogError("[Kue] Runtime moon catalog commit failed: " + commit);
                return;
            }
            moonCatalog.Clear();
            moonCatalog.AddRange(moonCatalogBuffer);
            moonLevelIndices.Clear();
            moonLevelIndices.AddRange(moonLevelBuffer);
            moonCatalogReady = true;
            Debug.Log("[Kue] Runtime moon catalog: " + moonCatalog.Count + " moons, " + routable +
                      " routable");
        }

        private void TravelToMoonManaged(int index)
        {
            StartOfRound round = StartOfRound.Instance;
            if (round == null || index < 0 || index >= moonCatalog.Count)
            {
                ReportActionFailure("Moon travel failed: catalog entry unavailable");
                return;
            }
            SelectableLevel level = moonCatalog[index];
            int levelIndex = moonLevelIndices[index];
            string name = MoonName(level, levelIndex < 0);
            if (levelIndex < 0)
            {
                ReportActionFailure("Moon travel failed: the ship has no route to " + name);
                return;
            }
            if (!round.inShipPhase)
            {
                ReportActionFailure("Moon travel failed: the ship must be in orbit");
                return;
            }
            if (round.travellingToNewLevel)
            {
                ReportActionFailure("Moon travel failed: the ship is already travelling");
                return;
            }
            Terminal terminal = FindOne("Terminal") as Terminal;
            if (terminal == null)
            {
                ReportActionFailure("Moon travel failed: terminal unavailable");
                return;
            }
            round.ChangeLevelServerRpc(levelIndex, terminal.groupCredits);
            Debug.Log("[Kue] Routing the ship to " + name + " (level " + levelIndex + ")");
            if (HUDManager.Instance != null)
                HUDManager.Instance.DisplayTip("Kue", "Routing to " + name);
        }

        private static int ItemCatalogSignature(StartOfRound round, int loadedCount)
        {
            int listed = round.allItemsList != null && round.allItemsList.itemsList != null
                             ? round.allItemsList.itemsList.Count
                             : 0;
            return listed * 65536 + loadedCount;
        }

        private static string CatalogName(string name)
        {
            return name.Length > 60 ? name.Substring(0, 60) : name;
        }

        private void RefreshCatalogLifecycle()
        {
            StartOfRound currentRound = StartOfRound.Instance;
            if (currentRound == catalogRound)
                return;
            catalogRound = currentRound;
            enemyCatalogReady = false;
            itemCatalogReady = false;
            moonCatalogReady = false;
            enemyCatalog.Clear();
            itemCatalog.Clear();
            moonCatalog.Clear();
            moonLevelIndices.Clear();
            enemyCatalogIndices.Clear();
            enemyCatalogNames.Clear();
            activeEnemyCounts = new int[0];
            reportedEnemyCounts = new int[0];
            NativeBridge.ResetRuntimeCatalogs();
        }

        private void RefreshItemCatalog()
        {
            if (itemCatalogReady)
                return;
            StartOfRound round = StartOfRound.Instance;
            if (round == null || round.allItemsList == null || round.allItemsList.itemsList == null)
                return;
            Item[] loadedItems = Resources.FindObjectsOfTypeAll<Item>();
            itemCatalogSignature = ItemCatalogSignature(round, loadedItems.Length);
            itemCatalogBuffer.Clear();
            catalogInstanceIds.Clear();
            foreach (Item item in round.allItemsList.itemsList)
            {
                if (item == null || item.spawnPrefab == null ||
                    string.IsNullOrEmpty(item.itemName) ||
                    !catalogInstanceIds.Add(item.GetInstanceID()))
                    continue;
                itemCatalogBuffer.Add(item);
            }
            foreach (Item item in loadedItems)
            {
                if (item == null || item.spawnPrefab == null ||
                    string.IsNullOrEmpty(item.itemName) ||
                    !catalogInstanceIds.Add(item.GetInstanceID()))
                    continue;
                itemCatalogBuffer.Add(item);
            }
            if (itemCatalogBuffer.Count == 0)
                return;
            itemCatalogBuffer.Sort(CompareItems);
            CatalogBeginResult begin = NativeBridge.BeginItemCatalog();
            if (begin != CatalogBeginResult.Begun)
            {
                Debug.LogError("[Kue] Runtime item catalog begin failed: " + begin);
                return;
            }
            usedCatalogNames.Clear();
            for (int i = 0; i < itemCatalogBuffer.Count; i++)
            {
                Item item = itemCatalogBuffer[i];
                CatalogReportResult report = NativeBridge.ReportItemType(
                    item.GetInstanceID(), UniqueCatalogName(item.itemName));
                if (report != CatalogReportResult.Recorded)
                {
                    NativeBridge.AbortItemCatalog();
                    Debug.LogError("[Kue] Runtime item catalog report failed at " + i + ": " +
                                   report);
                    return;
                }
            }
            CatalogCommitResult commit = NativeBridge.CommitItemCatalog();
            if (commit != CatalogCommitResult.Committed &&
                commit != CatalogCommitResult.Unchanged)
            {
                Debug.LogError("[Kue] Runtime item catalog commit failed: " + commit);
                return;
            }
            itemCatalog.Clear();
            itemCatalog.AddRange(itemCatalogBuffer);
            itemCatalogReady = true;
            Debug.Log("[Kue] Runtime item catalog: " + itemCatalog.Count + " installed types");
        }

        private void UpdatePlayerUtilities()
        {
            bool infiniteBattery = Flag(13);
            bool extendedInventory = Flag(14);
            if (!infiniteBattery && !extendedInventory)
                return;
            PlayerControllerB local = LocalPlayer();
            if (local == null)
                return;
            if (infiniteBattery)
            {
                if (local.ItemSlots != null)
                    foreach (GrabbableObject item in local.ItemSlots)
                        if (item != null && item.insertedBattery != null)
                            item.insertedBattery.charge = 1f;
                if (local.ItemOnlySlot != null && local.ItemOnlySlot.insertedBattery != null)
                    local.ItemOnlySlot.insertedBattery.charge = 1f;
            }
            if (extendedInventory)
                EnsureInventoryCapacity(local);
        }

        private void EnsureInventoryCapacity(PlayerControllerB local)
        {
            if (local.ItemSlots == null)
                return;
            int used = 0;
            foreach (GrabbableObject item in local.ItemSlots)
                if (item != null)
                    used++;
            if (local.ItemSlots.Length > 4 && local.ItemSlots.Length - used >= 4)
                return;
            int oldLength = local.ItemSlots.Length;
            int newLength = oldLength <= 4 ? 32 : oldLength + 4;
            if (newLength <= oldLength)
                return;
            Array.Resize(ref local.ItemSlots, newLength);
            ExpandInventoryHud(oldLength, newLength);
            Debug.Log("[Kue] Inventory expanded to " + newLength + " slots");
        }

        private void ExpandInventoryHud(int oldLength, int newLength)
        {
            HUDManager hud = HUDManager.Instance;
            if (hud == null || hud.itemSlotIcons == null || hud.itemSlotIconFrames == null ||
                hud.itemSlotIcons.Length == 0 || hud.itemSlotIconFrames.Length == 0)
                return;
            if (hud.itemSlotIcons.Length >= newLength && hud.itemSlotIconFrames.Length >= newLength)
                return;
            Image[] oldIcons = hud.itemSlotIcons;
            Image[] oldFrames = hud.itemSlotIconFrames;
            Image[] icons = new Image[newLength];
            Image[] frames = new Image[newLength];
            Array.Copy(oldIcons, icons, Mathf.Min(oldIcons.Length, newLength));
            Array.Copy(oldFrames, frames, Mathf.Min(oldFrames.Length, newLength));
            Image iconTemplate = oldIcons[Mathf.Min(oldIcons.Length - 1, 3)];
            Image frameTemplate = oldFrames[Mathf.Min(oldFrames.Length - 1, 3)];
            Vector3 iconBase = oldIcons[0].rectTransform.localPosition;
            Vector3 frameBase = oldFrames[0].rectTransform.localPosition;
            float stepX = oldFrames.Length > 1
                              ? oldFrames[1].rectTransform.localPosition.x - frameBase.x
                              : 50f;
            if (Mathf.Abs(stepX) < 1f)
                stepX = 50f;
            for (int i = oldLength; i < newLength; i++)
            {
                int column = i % 8;
                int row = i / 8;
                frames[i] = Instantiate(frameTemplate, frameTemplate.transform.parent);
                icons[i] = Instantiate(iconTemplate, iconTemplate.transform.parent);
                frames[i].name = "KueInventoryFrame" + i;
                icons[i].name = "KueInventoryIcon" + i;
                frames[i].rectTransform.localPosition =
                    frameBase + new Vector3(stepX * column, -48f * row, 0f);
                icons[i].rectTransform.localPosition =
                    iconBase + new Vector3(stepX * column, -48f * row, 0f);
                icons[i].enabled = false;
            }
            hud.itemSlotIcons = icons;
            hud.itemSlotIconFrames = frames;
        }

        private void SpawnEnemyManaged(int packed)
        {
            int index = packed & 0xffff;
            int count = Mathf.Clamp((packed >> 16) & 0xff, 1, 20);
            bool outside = (packed & (1 << 30)) != 0;
            PlayerControllerB local = LocalPlayer();
            if (index < 0 || index >= enemyCatalog.Count || RoundManager.Instance == null ||
                StartOfRound.Instance == null || local == null)
            {
                ReportActionFailure("Enemy spawn failed: session or type unavailable");
                return;
            }
            if (!local.IsHost)
            {
                ReportActionFailure("Networked enemy spawning requires host");
                return;
            }
            EnemyType type = enemyCatalog[index];
            bool spawnOutside = type.isOutsideEnemy || type.isDaytimeEnemy;
            List<GameObject> nodes = LiveNodes(spawnOutside);
            if (nodes.Count == 0)
            {
                spawnOutside = !spawnOutside;
                nodes = LiveNodes(spawnOutside);
            }
            if (nodes.Count == 0)
            {
                ReportActionFailure(
                    "Enemy spawn failed: no navigation nodes, land on a moon first");
                return;
            }
            StartOfRound.Instance.currentLevel.maxEnemyPowerCount = int.MaxValue;
            List<EnemyAI> spawned = new List<EnemyAI>();
            for (int i = 0; i < count; i++)
            {
                Vector3 position =
                    nodes[UnityEngine.Random.Range(0, nodes.Count)].transform.position;
                if (string.Equals(type.enemyName, "Bush Wolf", StringComparison.OrdinalIgnoreCase))
                    StartCoroutine(SpawnBushWolfManaged());
                else
                {
                    SpawnNestForEnemy(type, position);
                    NetworkObjectReference reference =
                        RoundManager.Instance.SpawnEnemyGameObject(position, 0f, -1, type);
                    NetworkObject networkObject;
                    if (reference.TryGet(out networkObject) && networkObject != null)
                    {
                        EnemyAI enemy = networkObject.GetComponentInParent<EnemyAI>();
                        if (enemy != null)
                            spawned.Add(enemy);
                    }
                }
            }
            if (spawned.Count > 0)
                StartCoroutine(ApplyHabitatAfterStart(spawned, spawnOutside));
            string area = spawnOutside ? " outside" : " inside";
            Debug.Log("[Kue] Spawned " + count + " x " + type.enemyName + area +
                      (spawnOutside != outside ? " (enemy habitat)" : ""));
            if (HUDManager.Instance != null)
                HUDManager.Instance.DisplayTip("Kue", "Spawned " + count + " " + type.enemyName +
                                                          area);
        }

        private static List<GameObject> LiveNodes(bool outside)
        {
            List<GameObject> live = new List<GameObject>();
            GameObject[] nodes = outside ? RoundManager.Instance.outsideAINodes
                                         : RoundManager.Instance.insideAINodes;
            if (nodes != null)
                foreach (GameObject node in nodes)
                    if (node != null)
                        live.Add(node);
            return live;
        }

        private static Vector3 SnapToNavMesh(Vector3 position)
        {
            NavMeshHit hit;
            if (NavMesh.SamplePosition(position, out hit, 6f, NavMesh.AllAreas) ||
                NavMesh.SamplePosition(position, out hit, 40f, NavMesh.AllAreas))
                return hit.position;
            return position;
        }

        private void SpawnNestForEnemy(EnemyType type, Vector3 position)
        {
            if (type.nestSpawnPrefab == null || RoundManager.Instance == null ||
                RoundManager.Instance.enemyNestSpawnObjects == null)
                return;
            GameObject nest = Instantiate(type.nestSpawnPrefab, position, Quaternion.identity);
            NetworkObject networkObject = nest.GetComponentInChildren<NetworkObject>();
            EnemyAINestSpawnObject spawnObject = nest.GetComponent<EnemyAINestSpawnObject>();
            if (networkObject == null || spawnObject == null)
            {
                Destroy(nest);
                return;
            }
            networkObject.Spawn(true);
            RoundManager.Instance.enemyNestSpawnObjects.Insert(0, spawnObject);
            type.nestsSpawned++;
            StartCoroutine(DespawnNestAfterUse(spawnObject));
        }

        private IEnumerator DespawnNestAfterUse(EnemyAINestSpawnObject nest)
        {
            yield return null;
            yield return null;
            if (nest == null)
                yield break;
            if (RoundManager.Instance != null &&
                RoundManager.Instance.enemyNestSpawnObjects != null)
                RoundManager.Instance.enemyNestSpawnObjects.Remove(nest);
            NetworkObject networkObject = nest.GetComponentInChildren<NetworkObject>();
            if (networkObject != null && networkObject.IsSpawned)
                networkObject.Despawn(true);
            else
                Destroy(nest.gameObject);
        }

        private IEnumerator SpawnBushWolfManaged()
        {
            MoldSpreadManager mold = UnityEngine.Object.FindObjectOfType<MoldSpreadManager>();
            GameObject[] nodes =
                RoundManager.Instance == null ? null : RoundManager.Instance.outsideAINodes;
            if (mold == null || nodes == null || nodes.Length == 0)
                yield break;
            mold.GenerateMold(nodes[UnityEngine.Random.Range(0, nodes.Length)].transform.position,
                              1);
            yield return new WaitForSeconds(0.5f);
            mold.RemoveAllMold();
        }

        private void SpawnEnemyAtPlayerManaged(int packed)
        {
            int index = packed & 0xfff;
            int count = Mathf.Clamp((packed >> 12) & 0x1f, 1, 20);
            bool outside = (packed & (1 << 17)) != 0;
            int clientId = (packed >> 18) & 0xfff;
            PlayerControllerB target = PlayerByClientId(clientId);
            PlayerControllerB local = LocalPlayer();
            if (target == null || local == null || index < 0 || index >= enemyCatalog.Count ||
                RoundManager.Instance == null)
            {
                Debug.LogError("[Kue] Targeted enemy spawn rejected: client=" + clientId +
                               " target=" + (target == null ? "null" : target.playerUsername) +
                               " host=" + (local != null && local.IsHost) + " type=" + index +
                               "/" + enemyCatalog.Count);
                if (HUDManager.Instance != null)
                    HUDManager.Instance.DisplayTip("Kue", "Enemy spawn failed: target unavailable");
                return;
            }

            if (!local.IsHost)
            {
                Debug.LogWarning("[Kue] Targeted network enemy spawn requires host; local client=" +
                                 local.actualClientId + " target=" + target.playerUsername);
                if (HUDManager.Instance != null)
                    HUDManager.Instance.DisplayTip("Kue", "Networked enemy spawning requires host");
                return;
            }
            EnemyType type = enemyCatalog[index];
            if (LiveNodes(true).Count == 0 && LiveNodes(false).Count == 0)
            {
                ReportActionFailure("Enemy spawn failed: no moon loaded, land first");
                return;
            }
            Vector3 position = SnapToNavMesh(TargetPosition(target) + TargetForward(target) * 2f);
            List<EnemyAI> spawned = new List<EnemyAI>();
            for (int i = 0; i < count; i++)
            {
                Vector3 offset = UnityEngine.Random.insideUnitSphere * 1.5f;
                offset.y = 0f;
                if (string.Equals(type.enemyName, "Bush Wolf", StringComparison.OrdinalIgnoreCase))
                    StartCoroutine(SpawnBushWolfAtManaged(position + offset));
                else
                {
                    SpawnNestForEnemy(type, position + offset);
                    NetworkObjectReference reference =
                        RoundManager.Instance.SpawnEnemyGameObject(position + offset, 0f, -1, type);
                    NetworkObject networkObject;
                    if (reference.TryGet(out networkObject) && networkObject != null)
                    {
                        EnemyAI enemy = networkObject.GetComponentInParent<EnemyAI>();
                        if (enemy == null)
                            enemy = networkObject.GetComponentInChildren<EnemyAI>();
                        if (enemy != null)
                            spawned.Add(enemy);
                    }
                }
            }
            if (spawned.Count > 0)
                StartCoroutine(StabilizeEnemyTeleport(spawned, target, 6));
            Debug.Log("[Kue] Spawned " + count + " x " + type.enemyName + " at " +
                      target.playerUsername + " client=" + clientId + " position=" + position +
                      " requestedOutside=" + outside + " resolved=" + spawned.Count);
            if (HUDManager.Instance != null)
                HUDManager.Instance.DisplayTip("Kue", "Spawned " + count + " " + type.enemyName +
                                                          " at " + target.playerUsername);
        }

        private IEnumerator SpawnBushWolfAtManaged(Vector3 position)
        {
            MoldSpreadManager mold = UnityEngine.Object.FindObjectOfType<MoldSpreadManager>();
            if (mold == null)
                yield break;
            mold.GenerateMold(position, 1);
            yield return new WaitForSeconds(0.5f);
            mold.RemoveAllMold();
        }

        private void SummonEnemyTypeAtPlayerManaged(int packed)
        {
            int index = packed & 0xfff;
            int requested = Mathf.Clamp((packed >> 12) & 0x1f, 1, 20);
            int clientId = (packed >> 18) & 0xfff;
            PlayerControllerB target = PlayerByClientId(clientId);
            PlayerControllerB local = LocalPlayer();
            if (target == null || local == null || index < 0 || index >= enemyCatalog.Count)
            {
                if (HUDManager.Instance != null)
                    HUDManager.Instance.DisplayTip("Kue", "Summon failed: target/type unavailable");
                return;
            }

            EnemyType selectedType = enemyCatalog[index];
            List<EnemyAI> existing = new List<EnemyAI>();
            foreach (EnemyAI enemy in UnityEngine.Object.FindObjectsOfType<EnemyAI>())
            {
                if (existing.Count >= requested)
                    break;
                if (enemy == null || enemy.isEnemyDead || enemy.enemyType == null)
                    continue;
                bool match = enemy.enemyType == selectedType ||
                             string.Equals(enemy.enemyType.enemyName, selectedType.enemyName,
                                           StringComparison.OrdinalIgnoreCase);
                if (!match)
                    continue;
                try
                {
                    if (local.IsHost)
                        enemy.ChangeOwnershipOfEnemy(local.actualClientId);
                    else
                        enemy.ChangeEnemyOwnerServerRpc(local.actualClientId);
                    existing.Add(enemy);
                }
                catch (Exception e)
                {
                    Debug.LogError("[Kue] Selected enemy summon ownership failed: " + e);
                }
            }

            if (existing.Count > 0)
                StartCoroutine(StabilizeEnemyTeleport(existing, target, local.IsHost ? 8 : 90));

            int missing = requested - existing.Count;
            if (missing > 0 && local.IsHost)
            {
                int spawnPacked = (index & 0xfff) | ((missing & 0x1f) << 12) |
                                  (packed & (1 << 17)) | ((clientId & 0xfff) << 18);
                SpawnEnemyAtPlayerManaged(spawnPacked);
            }

            string result = "Moved " + existing.Count + " existing " + selectedType.enemyName;
            if (missing > 0)
                result += local.IsHost ? "; spawning " + missing + " missing"
                                       : "; " + missing + " not present (host required to create)";
            Debug.Log("[Kue] " + result + " to " + target.playerUsername);
            if (HUDManager.Instance != null)
                HUDManager.Instance.DisplayTip("Kue", result);
        }

        private void TeleportAllEnemiesManaged(PlayerControllerB target)
        {
            PlayerControllerB local = LocalPlayer();
            if (target == null || local == null)
            {
                Debug.LogError("[Kue] Enemy teleport rejected: target=" +
                               (target == null ? "null" : target.playerUsername) +
                               " host=" + (local != null && local.IsHost));
                if (HUDManager.Instance != null)
                    HUDManager.Instance.DisplayTip("Kue", "Enemy teleport target unavailable");
                return;
            }
            List<EnemyAI> enemies = new List<EnemyAI>();
            foreach (EnemyAI enemy in UnityEngine.Object.FindObjectsOfType<EnemyAI>())
            {
                if (enemy == null || enemy.isEnemyDead)
                    continue;
                try
                {
                    if (local.IsHost)
                        enemy.ChangeOwnershipOfEnemy(local.actualClientId);
                    else
                        enemy.ChangeEnemyOwnerServerRpc(local.actualClientId);
                    enemies.Add(enemy);
                }
                catch (Exception e)
                {
                    Debug.LogError("[Kue] Enemy teleport ownership failed: " + e);
                }
            }
            StartCoroutine(StabilizeEnemyTeleport(enemies, target, local.IsHost ? 8 : 90));
        }

        private static bool MoveEnemyAndSync(EnemyAI enemy, Vector3 position,
                                             PlayerControllerB local)
        {
            if (enemy == null || enemy.isEnemyDead)
                return false;
            if (enemy.thisNetworkObject != null &&
                enemy.thisNetworkObject.OwnerClientId != local.actualClientId)
                return false;

            Vector3 previous = enemy.transform.position;
            bool warped = false;
            NavMeshAgent agent = enemy.agent;
            if (agent != null && agent.enabled && agent.isOnNavMesh)
                warped = agent.Warp(position);
            if (!warped)
                enemy.transform.position = position;

            enemy.serverPosition = previous;
            enemy.SyncPositionToClients();
            return true;
        }

        private static void SyncEnemyHabitat(EnemyAI enemy, bool outside)
        {
            if (enemy.isOutside == outside)
                return;
            enemy.SetEnemyOutside(outside);
            Debug.Log("[Kue] " + enemy.name + " switched to hunting " +
                      (outside ? "outside" : "inside"));
        }

        private IEnumerator ApplyHabitatAfterStart(List<EnemyAI> enemies, bool outside)
        {
            yield return null;
            yield return null;
            foreach (EnemyAI enemy in enemies)
                if (enemy != null && !enemy.isEnemyDead && enemy.path1 != null)
                    SyncEnemyHabitat(enemy, outside);
        }

        private IEnumerator StabilizeEnemyTeleport(List<EnemyAI> enemies, PlayerControllerB target,
                                                   int frames)
        {
            PlayerControllerB local = LocalPlayer();
            if (local == null || target == null)
                yield break;
            int moved = 0;
            HashSet<int> movedEnemies = new HashSet<int>();
            for (int frame = 0; frame < frames; frame++)
            {
                Vector3 center =
                    SnapToNavMesh(TargetPosition(target) + TargetForward(target) * 2f);
                int layoutIndex = 0;
                foreach (EnemyAI enemy in enemies)
                {
                    if (enemy == null || enemy.isEnemyDead)
                        continue;
                    float angle = layoutIndex * 0.7f;
                    float radius = 1f + (layoutIndex / 9) * 0.8f;
                    layoutIndex++;
                    Vector3 position = center + new Vector3(Mathf.Cos(angle) * radius, 0f,
                                                            Mathf.Sin(angle) * radius);
                    try
                    {
                        bool owned = enemy.thisNetworkObject != null &&
                                     enemy.thisNetworkObject.OwnerClientId == local.actualClientId;
                        if (!owned && !local.IsHost && frame % 15 == 0)
                            enemy.ChangeEnemyOwnerServerRpc(local.actualClientId);
                        if (MoveEnemyAndSync(enemy, position, local))
                            movedEnemies.Add(enemy.GetInstanceID());
                        if (owned && enemy.path1 != null)
                            SyncEnemyHabitat(enemy, !target.isInsideFactory);
                    }
                    catch (Exception e)
                    {
                        Debug.LogError("[Kue] Enemy teleport sync failed for " + enemy.name + ": " +
                                       e);
                    }
                }
                yield return null;
            }
            moved = movedEnemies.Count;
            Debug.Log("[Kue] Authoritatively teleported " + moved + " enemies to " +
                      target.playerUsername + " client=" + target.playerClientId +
                      " position=" + TargetPosition(target));
            if (HUDManager.Instance != null)
                HUDManager.Instance.DisplayTip("Kue", "Teleported " + moved + " enemies to " +
                                                          target.playerUsername);
        }

        private void SpawnItemAtPlayerManaged(int packed)
        {
            int index = packed & 0xfff;
            int count = Mathf.Clamp((packed >> 12) & 0x1f, 1, 20);
            int clientId = (packed >> 17) & 0xfff;
            PlayerControllerB target = PlayerByClientId(clientId);
            PlayerControllerB local = LocalPlayer();
            if (target == null || local == null || StartOfRound.Instance == null)
            {
                ReportActionFailure("Item spawn failed: target player unavailable");
                return;
            }
            if (!local.IsHost)
            {
                ReportActionFailure("Item spawning requires host");
                return;
            }
            if (index < 0 || index >= itemCatalog.Count)
            {
                ReportActionFailure("Item spawn failed: catalog entry " + index + " of " +
                                    itemCatalog.Count + " unavailable");
                return;
            }
            Item item = itemCatalog[index];
            Vector3 position =
                target.transform.position + Vector3.up * 1.2f + target.transform.forward * 1.5f;
            int spawned = 0;
            for (int i = 0; i < count; i++)
            {
                GameObject gameObject = Instantiate(
                    item.spawnPrefab, position + UnityEngine.Random.insideUnitSphere * 0.25f,
                    Quaternion.identity, StartOfRound.Instance.propsContainer);
                GrabbableObject grabbable = gameObject.GetComponent<GrabbableObject>();
                NetworkObject networkObject = gameObject.GetComponent<NetworkObject>();
                if (grabbable == null || networkObject == null)
                {
                    Destroy(gameObject);
                    continue;
                }
                if (item.isScrap)
                    grabbable.SetScrapValue(
                        UnityEngine.Random.Range(Mathf.Max(0, item.minValue),
                                                 Mathf.Max(item.minValue + 1, item.maxValue + 1)));
                networkObject.Spawn(false);
                PlaceItemAtPlayer(grabbable, target, position);
                spawned++;
            }
            if (spawned == 0)
            {
                ReportActionFailure("Item spawn failed: " + item.itemName +
                                    " has no grabbable network prefab");
                return;
            }
            Debug.Log("[Kue] Spawned " + spawned + " x " + item.itemName + " at " +
                      target.playerUsername + " position=" + position);
            if (HUDManager.Instance != null)
                HUDManager.Instance.DisplayTip("Kue", "Spawned " + spawned + " " + item.itemName);
        }

        private static void PlaceItemAtPlayer(GrabbableObject item, PlayerControllerB target,
                                              Vector3 position)
        {
            if (item == null || target == null || StartOfRound.Instance == null)
                return;
            item.transform.SetParent(StartOfRound.Instance.propsContainer, true);
            item.transform.position = position;
            target.SetItemInElevator(target.isInHangarShipRoom, target.isInElevator, item);
            Vector3 localPosition = item.transform.parent.InverseTransformPoint(position);
            item.startFallingPosition = localPosition;
            item.targetFloorPosition = localPosition;
            item.fallTime = 0f;
            item.EnablePhysics(true);
        }

        private void TeleportItemsToPlayerManaged(PlayerControllerB target)
        {
            if (!RealPlayer(target))
                return;
            Vector3 center =
                target.transform.position + target.transform.forward * 1.5f + Vector3.up * 0.5f;
            int moved = 0;
            int denied = 0;
            foreach (GrabbableObject item in UnityEngine.Object
                         .FindObjectsOfType<GrabbableObject>())
            {
                if (item == null || item.isHeld || item.isPocketed || item.heldByPlayerOnServer)
                    continue;
                if (!TakeOwnership(item))
                {
                    denied++;
                    continue;
                }
                Vector3 offset = new Vector3((moved % 5) * 0.25f, (moved / 5) * 0.1f, 0f);
                PlaceItemAtPlayer(item, target, center + offset);
                moved++;
            }
            Debug.Log("[Kue] Teleported " + moved + " loaded items to " + target.playerUsername);
            if (denied > 0)
                ReportActionFailure("Item teleport moved " + moved + "; ownership denied for " +
                                    denied);
        }

        private void KillAllEnemiesManaged()
        {
            PlayerControllerB local = LocalPlayer();
            List<EnemyAI> enemies = new List<EnemyAI>();
            foreach (EnemyAI enemy in UnityEngine.Object.FindObjectsOfType<EnemyAI>())
            {
                if (enemy == null || enemy.isEnemyDead)
                    continue;
                try
                {
                    if (local != null)
                        enemy.ChangeEnemyOwnerServerRpc(local.actualClientId);
                    enemies.Add(enemy);
                }
                catch (Exception e)
                {
                    Debug.LogError("[Kue] Enemy kill failed: " + e);
                }
            }
            StartCoroutine(KillEnemiesAfterOwnership(enemies));
        }

        private IEnumerator KillEnemiesAfterOwnership(List<EnemyAI> enemies)
        {
            yield return new WaitForSeconds(0.15f);
            foreach (EnemyAI enemy in enemies)
            {
                if (enemy == null || enemy.isEnemyDead)
                    continue;
                try
                {
                    enemy.KillEnemyOnOwnerClient(true);
                }
                catch (Exception e)
                {
                    Debug.LogError("[Kue] Deferred enemy kill failed: " + e);
                }
            }
        }

        private void StunAllEnemiesManaged()
        {
            PlayerControllerB local = LocalPlayer();
            foreach (EnemyAI enemy in UnityEngine.Object.FindObjectsOfType<EnemyAI>())
            {
                if (enemy == null || enemy.isEnemyDead)
                    continue;
                try
                {
                    enemy.SetEnemyStunned(true, 60f, local);
                }
                catch (Exception e)
                {
                    Debug.LogError("[Kue] Enemy stun failed: " + e);
                }
            }
        }

        private void SetFly(bool enabled)
        {
            PlayerControllerB local = LocalPlayer();
            if (local == null && enabled)
                return;
            if (!enabled)
            {
                RestoreFlyState();
                flyEnabled = false;
                NativeBridge.ReportFlyState(0);
                return;
            }
            if (flyPlayer != local)
            {
                RestoreFlyState();
                flyPlayer = local;
                flyController = local.GetComponent<CharacterController>();
            }
            if (flyController != null)
                flyController.enabled = false;
            flyEnabled = true;
            NativeBridge.ReportFlyState(1);
        }

        private void RestoreFlyState()
        {
            RestoreFlapBones();
            if (flyController != null)
                flyController.enabled = true;
            if (flyPlayer != null)
                flyPlayer.ResetFallGravity();
            flyController = null;
            flyPlayer = null;
        }

        private void UpdateFly()
        {
            if (!flyEnabled)
                return;
            PlayerControllerB local = LocalPlayer();
            Keyboard keyboard = Keyboard.current;
            if (local == null || keyboard == null || local.isPlayerDead)
            {
                SetFly(false);
                return;
            }
            if (flyPlayer != local)
            {
                SetFly(false);
                return;
            }
            if (menuOpen)
                return;
            Transform view =
                local.gameplayCamera != null ? local.gameplayCamera.transform : local.transform;
            Vector3 forward = Vector3.ProjectOnPlane(view.forward, Vector3.up).normalized;
            Vector3 right = Vector3.ProjectOnPlane(view.right, Vector3.up).normalized;
            Vector3 move = Vector3.zero;
            if (keyboard.wKey.isPressed)
                move += forward;
            if (keyboard.sKey.isPressed)
                move -= forward;
            if (keyboard.dKey.isPressed)
                move += right;
            if (keyboard.aKey.isPressed)
                move -= right;
            if (keyboard.spaceKey.isPressed)
                move += Vector3.up;
            if (keyboard.leftCtrlKey.isPressed || keyboard.cKey.isPressed)
                move -= Vector3.up;
            if (move.sqrMagnitude > 1f)
                move.Normalize();
            local.transform.position += move * flySpeed * Time.unscaledDeltaTime;
            local.serverPlayerPosition = local.transform.position;
            local.fallValue = 0f;
            local.fallValueUncapped = 0f;
            local.takingFallDamage = false;
        }

        private void ForceTentacleAttackManaged()
        {
            DepositItemsDesk desk = FindOne("DepositItemsDesk") as DepositItemsDesk;
            if (desk == null)
            {
                ReportActionFailure("Tentacle attack failed: the company desk is not loaded");
                return;
            }
            if (desk.currentMood == null || desk.currentMood.enableMonsterAnimationIndex == null)
            {
                ReportActionFailure("Tentacle attack failed: the company's mood has no monster");
                return;
            }
            if (desk.attacking || desk.inGrabbingObjectsAnimation)
            {
                ReportActionFailure("Tentacle attack skipped: the company desk is busy");
                return;
            }
            PlayerControllerB local = LocalPlayer();
            if (local != null && local.IsHost)
            {
                desk.AttackPlayersClientRpc();
                StartCoroutine(SettleDeskAfterAttack(desk));
            }
            else
                desk.AttackPlayersServerRpc();
        }

        private IEnumerator SettleDeskAfterAttack(DepositItemsDesk desk)
        {
            float deadline = Time.unscaledTime + 12f;
            yield return new WaitForSeconds(0.5f);
            while (desk != null && desk.attacking && Time.unscaledTime < deadline)
                yield return null;
            yield return new WaitForSeconds(2f);
            if (desk == null || desk.attacking)
                yield break;
            desk.timeSinceAttacking = 0f;
            if (desk.doorOpen && desk.itemsOnCounter.Count == 0)
                desk.OpenShutDoorClientRpc(false);
        }

        private void DepositShipScrapManaged()
        {
            DepositItemsDesk desk = FindOne("DepositItemsDesk") as DepositItemsDesk;
            PlayerControllerB local = LocalPlayer();
            if (desk == null || desk.deskObjectsContainer == null || desk.triggerCollider == null ||
                local == null)
            {
                ReportActionFailure("Deposit failed: the company desk is not loaded");
                return;
            }
            Transform container = desk.deskObjectsContainer.transform;
            Bounds counter = desk.triggerCollider.bounds;
            int moved = 0;
            int denied = 0;
            foreach (GrabbableObject item in UnityEngine.Object
                         .FindObjectsOfType<GrabbableObject>())
            {
                if (item == null || item.itemProperties == null || !item.itemProperties.isScrap ||
                    item.isHeld || item.isPocketed || item.heldByPlayerOnServer ||
                    item.deactivated || (!item.isInShipRoom && !item.isInElevator) ||
                    item.transform.IsChildOf(container))
                    continue;
                NetworkObject networkObject = item.GetComponent<NetworkObject>();
                if (networkObject == null)
                    continue;
                if (!TakeOwnership(item))
                {
                    denied++;
                    continue;
                }
                Vector3 point = RoundManager.RandomPointInBounds(counter);
                point.y = counter.min.y + item.itemProperties.verticalOffset;
                Vector3 offset = container.InverseTransformPoint(point);
                local.PlaceGrabbableObject(container, offset, false, item);
                item.isInShipRoom = false;
                item.isInElevator = false;
                desk.AddObjectToDeskServerRpc(networkObject);
                moved++;
            }
            Debug.Log("[Kue] Deposited " + moved + " scrap items on the company desk");
            if (moved == 0 && denied == 0)
                ReportActionFailure("Deposit found no scrap in the ship");
            else if (denied > 0)
                ReportActionFailure("Deposit moved " + moved + "; ownership denied for " + denied);
        }

        private void AddTerminalCreditsManaged(int amount)
        {
            Terminal terminal = FindOne("Terminal") as Terminal;
            PlayerControllerB local = LocalPlayer();
            if (terminal == null || local == null || amount <= 0)
            {
                ReportActionFailure("Credits failed: the terminal is not loaded");
                return;
            }
            int credits = Mathf.Clamp(terminal.groupCredits + amount, 0, 10000000);
            if (!local.IsHost)
            {
                ReportActionFailure("Credits failed: only the host owns the terminal balance");
                return;
            }
            terminal.groupCredits = credits;
            terminal.SyncGroupCreditsClientRpc(credits, terminal.numberOfItemsInDropship);
            Debug.Log("[Kue] Terminal credits set to " + credits);
        }

        private void SetThirdPerson(bool enabled)
        {
            PlayerControllerB local = LocalPlayer();
            if (thirdPersonPlayer != null && (!enabled || thirdPersonPlayer != local))
            {
                if (!thirdPersonPlayer.isPlayerDead && thirdPersonPlayer.thisPlayerModel != null)
                    thirdPersonPlayer.thisPlayerModel.shadowCastingMode =
                        ShadowCastingMode.ShadowsOnly;
                if (!thirdPersonPlayer.isPlayerDead && thirdPersonPlayer.thisPlayerModelArms != null)
                    thirdPersonPlayer.thisPlayerModelArms.enabled = true;
                thirdPersonPlayer = null;
            }
            thirdPersonEnabled = enabled && local != null;
            if (thirdPersonEnabled)
                thirdPersonPlayer = local;
            else
                RestoreThirdPersonCamera();
        }

        private void RestoreThirdPersonCamera()
        {
            if (!thirdPersonOffsetApplied)
                return;
            thirdPersonOffsetApplied = false;
            if (thirdPersonCamera != null)
                thirdPersonCamera.transform.position -= thirdPersonAppliedOffset;
            thirdPersonAppliedOffset = Vector3.zero;
        }

        private void UpdateThirdPersonModel()
        {
            if (!thirdPersonEnabled)
                return;
            PlayerControllerB local = LocalPlayer();
            if (local == null || local.isPlayerDead || local != thirdPersonPlayer)
            {
                SetThirdPerson(false);
                return;
            }
            if (local.thisPlayerModel != null &&
                local.thisPlayerModel.shadowCastingMode != ShadowCastingMode.On)
                local.thisPlayerModel.shadowCastingMode = ShadowCastingMode.On;
            if (local.thisPlayerModelArms != null && local.thisPlayerModelArms.enabled)
                local.thisPlayerModelArms.enabled = false;
        }

        private void OnBeginCameraRendering(ScriptableRenderContext context, Camera camera)
        {
            RestoreThirdPersonCamera();
            if (!thirdPersonEnabled || thirdPersonPlayer == null || camera == null ||
                camera != thirdPersonPlayer.gameplayCamera)
                return;
            Transform view = camera.transform;
            Vector3 eye = view.position;
            Vector3 desired = eye - view.forward * ThirdPersonDistance +
                              view.up * ThirdPersonHeight + view.right * ThirdPersonSide;
            RaycastHit hit;
            int mask = StartOfRound.Instance != null
                           ? StartOfRound.Instance.collidersAndRoomMaskAndDefault
                           : Physics.DefaultRaycastLayers;
            if (Physics.Linecast(eye, desired, out hit, mask, QueryTriggerInteraction.Ignore))
                desired = hit.point + (eye - desired).normalized * 0.15f;
            thirdPersonCamera = camera;
            thirdPersonAppliedOffset = desired - eye;
            thirdPersonOffsetApplied = true;
            view.position = desired;
        }

        private void OnEndCameraRendering(ScriptableRenderContext context, Camera camera)
        {
            RestoreThirdPersonCamera();
        }

        private static bool IsUpperArmBone(Transform bone)
        {
            string name = bone.name.ToLowerInvariant();
            if (!name.Contains("arm") || name.Contains("fore") || name.Contains("lower") ||
                name.Contains("hand") || name.Contains("finger") || name.Contains("metarig"))
                return false;
            return true;
        }

        private static float BoneSide(Transform bone)
        {
            string name = bone.name.ToLowerInvariant();
            return name.EndsWith(".r") || name.Contains(".r_") || name.Contains("right") ? -1f
                                                                                         : 1f;
        }

        private void CollectFlapBones(PlayerControllerB player)
        {
            flapBones.Clear();
            flapBoneSides.Clear();
            flapBaseRotations.Clear();
            flapAppliedRotations.Clear();
            flapPlayer = player;
            Transform[] roots = { player.playerModelArmsMetarig,
                                  player.thisPlayerModel != null ? player.thisPlayerModel.rootBone
                                                                 : null };
            foreach (Transform root in roots)
            {
                if (root == null)
                    continue;
                foreach (Transform bone in root.GetComponentsInChildren<Transform>(true))
                {
                    if (bone == root || !IsUpperArmBone(bone))
                        continue;
                    bool nested = false;
                    foreach (Transform chosen in flapBones)
                        if (bone.IsChildOf(chosen))
                            nested = true;
                    if (nested)
                        continue;
                    flapBones.Add(bone);
                    flapBoneSides.Add(BoneSide(bone));
                    flapBaseRotations.Add(bone.localRotation);
                    flapAppliedRotations.Add(bone.localRotation);
                }
            }
        }

        private void UpdateFlappingArms()
        {
            if (!flyEnabled || flyPlayer == null)
            {
                RestoreFlapBones();
                return;
            }
            if (flapPlayer != flyPlayer)
            {
                RestoreFlapBones();
                CollectFlapBones(flyPlayer);
            }
            Keyboard keyboard = Keyboard.current;
            bool climbing = keyboard != null && keyboard.spaceKey.isPressed;
            flapPhase += Time.unscaledDeltaTime * (climbing ? FlapClimbCycleSpeed : FlapCycleSpeed);
            float stroke = Mathf.Sin(flapPhase);
            float flap = (stroke * 0.5f + 0.5f) * FlapAmplitudeDegrees;
            float lift = Mathf.Cos(flapPhase) * FlapLiftDegrees;
            for (int i = 0; i < flapBones.Count; i++)
            {
                Transform bone = flapBones[i];
                if (bone == null)
                    continue;
                Quaternion current = bone.localRotation;
                Quaternion baseRotation =
                    current == flapAppliedRotations[i] ? flapBaseRotations[i] : current;
                Quaternion offset =
                    Quaternion.AngleAxis(flap * flapBoneSides[i], Vector3.forward) *
                    Quaternion.AngleAxis(lift, Vector3.right);
                bone.localRotation = baseRotation * offset;
                flapBaseRotations[i] = baseRotation;
                flapAppliedRotations[i] = bone.localRotation;
            }
        }

        private void RestoreFlapBones()
        {
            for (int i = 0; i < flapBones.Count; i++)
            {
                Transform bone = flapBones[i];
                if (bone != null && bone.localRotation == flapAppliedRotations[i])
                    bone.localRotation = flapBaseRotations[i];
            }
            flapBones.Clear();
            flapBoneSides.Clear();
            flapBaseRotations.Clear();
            flapAppliedRotations.Clear();
            flapPlayer = null;
        }

        private static UnityEngine.Object[] FindAll(string typeName)
        {
            ObjectCacheEntry cached;
            float now = Time.unscaledTime;
            if (GameObjectCache.TryGetValue(typeName, out cached) && cached.expiresAt > now)
                return cached.objects;
            Type type;
            if (!GameTypeCache.TryGetValue(typeName, out type))
            {
                type = typeof(StartOfRound).Assembly.GetType(typeName);
                GameTypeCache[typeName] = type;
            }
            UnityEngine.Object[] objects =
                type == null ? NoObjects : UnityEngine.Object.FindObjectsOfType(type);
            if (cached == null)
                cached = new ObjectCacheEntry();
            cached.objects = objects;
            cached.expiresAt = type == null ? float.MaxValue : now + 0.5f;
            GameObjectCache[typeName] = cached;
            return objects;
        }

        private static UnityEngine.Object FindOne(string typeName)
        {
            UnityEngine.Object[] all = FindAll(typeName);
            return all.Length == 0 ? null : all[0];
        }

        private static object InvokeAny(object target, string name)
        {
            return InvokeCached(target, name, null, 0);
        }

        private static object InvokeAny(object target, string name, object argument)
        {
            if (oneArgument == null)
                oneArgument = new object[1];
            oneArgument[0] = argument;
            try
            {
                return InvokeCached(target, name, oneArgument, 1);
            }
            finally
            {
                oneArgument[0] = null;
            }
        }

        private static object InvokeAny(object target, string name, object first, object second)
        {
            if (twoArguments == null)
                twoArguments = new object[2];
            twoArguments[0] = first;
            twoArguments[1] = second;
            try
            {
                return InvokeCached(target, name, twoArguments, 2);
            }
            finally
            {
                twoArguments[0] = null;
                twoArguments[1] = null;
            }
        }

        private static object InvokeCached(object target, string name, object[] arguments,
                                           int argumentCount)
        {
            if (target == null)
                return null;
            MethodCacheKey key = new MethodCacheKey {
                targetType = target.GetType(), name = name,
                firstArgument =
                    argumentCount > 0 && arguments[0] != null ? arguments[0].GetType() : null,
                secondArgument =
                    argumentCount > 1 && arguments[1] != null ? arguments[1].GetType() : null
            };
            MethodInfo method;
            if (MethodCache.TryGetValue(key, out method))
                return method == null ? null : method.Invoke(target, arguments);
            MethodInfo[] methods = key.targetType.GetMethods(
                BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
            foreach (MethodInfo candidate in methods)
            {
                if (candidate.Name != name || candidate.GetParameters().Length != argumentCount)
                    continue;
                try
                {
                    object result = candidate.Invoke(target, arguments);
                    MethodCache[key] = candidate;
                    return result;
                }
                catch (ArgumentException)
                {
                }
                catch (TargetParameterCountException)
                {
                }
            }
            MethodCache[key] = null;
            return null;
        }

        private static object GetMember(object target, string name)
        {
            if (target == null)
                return null;
            MemberAccessor accessor = GetAccessor(target.GetType(), name);
            if (accessor.field != null)
                return accessor.field.GetValue(target);
            return accessor.property == null ? null : accessor.property.GetValue(target, null);
        }

        private static void SetMember(object target, string name, object value)
        {
            if (target == null)
                return;
            MemberAccessor accessor = GetAccessor(target.GetType(), name);
            if (accessor.field != null)
            {
                accessor.field.SetValue(target, value);
                return;
            }
            if (accessor.property != null && accessor.property.CanWrite)
                accessor.property.SetValue(target, value, null);
        }

        private static MemberAccessor GetAccessor(Type type, string name)
        {
            MemberCacheKey key = new MemberCacheKey { targetType = type, name = name };
            MemberAccessor accessor;
            if (MemberCache.TryGetValue(key, out accessor))
                return accessor;
            accessor.field = type.GetField(name, BindingFlags.Instance | BindingFlags.Public |
                                                     BindingFlags.NonPublic);
            if (accessor.field == null)
                accessor.property = type.GetProperty(
                    name, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
            MemberCache[key] = accessor;
            return accessor;
        }

        private static bool BoolMember(object target, string name, bool fallback)
        {
            object value = GetMember(target, name);
            return value is bool ? (bool)value : fallback;
        }

        private static bool TakeOwnership(UnityEngine.Object target)
        {
            Component component = target as Component;
            PlayerControllerB local = LocalPlayer();
            if (component == null || local == null)
                return false;
            NetworkObject networkObject = component.GetComponent<NetworkObject>();
            if (networkObject == null)
                networkObject = component.GetComponentInParent<NetworkObject>();
            if (networkObject == null)
                networkObject = component.GetComponentInChildren<NetworkObject>();
            if (networkObject == null || !networkObject.IsSpawned)
                return true;
            if (networkObject.IsOwner)
                return true;
            if (!local.IsHost)
                return false;
            networkObject.ChangeOwnership(local.actualClientId);
            return true;
        }

        private void ToggleHorn(bool car)
        {
            if (car)
            {
                carHornEnabled = !carHornEnabled;
                foreach (UnityEngine.Object vehicle in FindAll("VehicleController"))
                    InvokeAny(vehicle, "SetHonkServerRpc", carHornEnabled, -1);
            }
            else
            {
                shipHornEnabled = !shipHornEnabled;
                ShipAlarmCord horn = FindOne("ShipAlarmCord") as ShipAlarmCord;
                if (horn == null)
                {
                    shipHornEnabled = false;
                    if (HUDManager.Instance != null)
                        HUDManager.Instance.DisplayTip("Kue", "Ship horn is not loaded");
                    return;
                }

                if (shipHornEnabled)
                    horn.HoldCordDown();
                else
                    horn.StopHorn();
                if (HUDManager.Instance != null)
                    HUDManager.Instance.DisplayTip(
                        "Kue", "Ship horn " + (shipHornEnabled ? "enabled" : "disabled"));
                Debug.Log("[Kue] Ship horn toggled " + shipHornEnabled);
            }
        }

        private void ForceEjectEveryoneManaged()
        {
            StartOfRound round = StartOfRound.Instance;
            PlayerControllerB local = LocalPlayer();
            if (round == null || local == null || !local.IsHost)
            {
                if (HUDManager.Instance != null)
                    HUDManager.Instance.DisplayTip("Kue", "Force eject is host-only");
                return;
            }
            round.ManuallyEjectPlayersServerRpc();
            Debug.Log("[Kue] Host requested manual player ejection");
        }

        private void SetPlayerInsanityManaged(PlayerControllerB player, bool maximum)
        {
            if (!RealPlayer(player))
            {
                ReportActionFailure("Insanity change failed: target unavailable");
                return;
            }
            PlayerControllerB local = LocalPlayer();
            if (player != local && (local == null || !local.IsHost))
            {
                ReportActionFailure("Remote insanity changes require host");
                return;
            }
            float value = maximum ? player.maxInsanityLevel : 0f;
            player.insanityLevel = value;
            Debug.Log("[Kue] Set insanity for " + player.playerUsername + " to " + value);
            if (HUDManager.Instance != null)
                HUDManager.Instance.DisplayTip(
                    "Kue", player.playerUsername + " insanity: " + Mathf.RoundToInt(value));
        }

        private void ShootAllShotgunsManaged()
        {
            foreach (UnityEngine.Object shotgun in FindAll("ShotgunItem"))
            {
                Component component = shotgun as Component;
                if (component == null)
                    continue;
                Transform t = component.transform;
                InvokeAny(shotgun, "ShootGunServerRpc", t.position, t.forward);
            }
        }

        private void ExplodeAllJetpacksManaged(bool heldOnly)
        {
            foreach (UnityEngine.Object jetpack in FindAll("JetpackItem"))
            {
                bool held = BoolMember(jetpack, "isHeld", false);
                if (heldOnly && !held)
                    continue;
                InvokeAny(jetpack, "ExplodeJetpackServerRpc");
            }
        }

        private void UpdateTrollToggles()
        {
            if (!slideTaunt && !pjManSpam && !terminalSpamEnabled && !depositSpamEnabled &&
                !shotgunSpamEnabled && !explodeJetpacksOnGrab && !shipHornEnabled &&
                !openShipDoorSpace)
                return;
            Keyboard keyboard = Keyboard.current;
            if (slideTaunt && keyboard != null &&
                (keyboard.digit1Key.wasPressedThisFrame || keyboard.digit2Key.wasPressedThisFrame))
                InvokeAny(LocalPlayer(), "StartPerformingEmoteServerRpc");
            if (pjManSpam && Time.unscaledTime >= nextPjManPulse)
            {
                nextPjManPulse = Time.unscaledTime + pjManSpamInterval;
                foreach (UnityEngine.Object trigger in FindAll("AnimatedObjectTrigger"))
                {
                    Component component = trigger as Component;
                    if (component != null && component.transform.parent != null &&
                        component.transform.parent.gameObject.name.StartsWith(
                            "PlushiePJManContainer"))
                        InvokeAny(trigger, "TriggerAnimation", LocalPlayer());
                }
            }
            if (Time.unscaledTime < nextTrollPulse)
                return;
            nextTrollPulse = Time.unscaledTime + 0.1f;
            if (terminalSpamEnabled)
            {
                UnityEngine.Object terminal = FindOne("Terminal");
                InvokeAny(terminal, "PlayTerminalAudioServerRpc", 1);
            }
            if (depositSpamEnabled)
            {
                UnityEngine.Object desk = FindOne("DepositItemsDesk");
                if (TakeOwnership(desk))
                    InvokeAny(desk, "OpenShutDoorClientRpc", true);
            }
            if (shotgunSpamEnabled)
                ShootAllShotgunsManaged();
            if (explodeJetpacksOnGrab)
                ExplodeAllJetpacksManaged(true);
            if (shipHornEnabled)
            {
                ShipAlarmCord horn = FindOne("ShipAlarmCord") as ShipAlarmCord;
                if (horn == null)
                    shipHornEnabled = false;
                else
                    horn.HoldCordDown();
            }
            if (openShipDoorSpace)
                InvokeAny(FindOne("HangarShipDoor"), "SetDoorButtonsEnabled", true);
        }

        private void SpawnMaskedManaged()
        {
            PlayerControllerB alive = null;
            StartOfRound round = StartOfRound.Instance;
            if (round != null && round.allPlayerScripts != null)
                foreach (PlayerControllerB player in round.allPlayerScripts)
                    if (RealPlayer(player) && !player.isPlayerDead)
                    {
                        alive = player;
                        break;
                    }
            foreach (UnityEngine.Object mask in FindAll("HauntedMaskItem"))
            {
                Component component = mask as Component;
                if (component == null)
                    continue;
                TakeOwnership(mask);
                InvokeAny(mask, "ChangeOwnershipOfProp",
                          LocalPlayer() == null ? 0UL : LocalPlayer().actualClientId);
                SetMember(mask, "previousPlayerHeldBy", alive);
                InvokeAny(mask, "CreateMimicServerRpc", BoolMember(mask, "isInFactory", false),
                          component.transform.position);
            }
        }

        private void ProcessTrollAction(int action, int payload)
        {
            if (action == 13)
                ToggleHorn(false);
            else if (action == 14)
                InvokeAny(FindOne("ShipLights"), "ToggleShipLights");
            else if (action == 15)
            {
                UnityEngine.Object breaker = FindOne("BreakerBox");
                if (RoundManager.Instance != null)
                {
                    RoundManager.Instance.powerOffPermanently = false;
                    RoundManager.Instance.SwitchPower(!BoolMember(breaker, "isPowerOn", true));
                }
            }
            else if (action == 16)
                foreach (UnityEngine.Object mine in FindAll("Landmine"))
                    InvokeAny(mine, "ExplodeMineServerRpc");
            else if (action == 17)
            {
                minesEnabled = !minesEnabled;
                foreach (UnityEngine.Object mine in FindAll("Landmine"))
                    InvokeAny(mine, "ToggleMine", minesEnabled);
            }
            else if (action == 18)
            {
                turretsEnabled = !turretsEnabled;
                foreach (UnityEngine.Object turret in FindAll("Turret"))
                    InvokeAny(turret, "ToggleTurretEnabled", turretsEnabled);
            }
            else if (action == 19)
            {
                turretsBerserk = !turretsBerserk;
                foreach (UnityEngine.Object turret in FindAll("Turret"))
                {
                    if (turretsBerserk)
                        InvokeAny(turret, "EnterBerserkModeServerRpc",
                                  LocalPlayer() == null ? -1 : (int)LocalPlayer().playerClientId);
                    else
                        InvokeAny(turret, "ToggleTurretEnabled", false);
                }
            }
            else if (action == 20)
                openShipDoorSpace = !openShipDoorSpace;
            else if (action == 21)
                ForceTentacleAttackManaged();
            else if (action == 45)
                DepositShipScrapManaged();
            else if (action == 46)
                AddTerminalCreditsManaged(payload);
            else if (action == 47)
                SetThirdPerson(!thirdPersonEnabled);
            else if (action == 48)
                TravelToMoonManaged(payload);
            else if (action == 22)
                SpawnMaskedManaged();
            else if (action == 23)
                ForceEjectEveryoneManaged();
            else if (action == 24)
            {
                int catalogIndex;
                if (!enemyCatalogNames.TryGetValue("Hoarding bug", out catalogIndex))
                    catalogIndex = -1;
                int rushIndex = -1;
                if (RoundManager.Instance != null && RoundManager.Instance.currentLevel != null)
                {
                    List<SpawnableEnemyWithRarity> enemies =
                        RoundManager.Instance.currentLevel.Enemies;
                    for (int i = 0; i < enemies.Count; i++)
                        if (enemies[i] != null && enemies[i].enemyType != null &&
                            string.Equals(enemies[i].enemyType.enemyName, "Hoarding bug",
                                          StringComparison.OrdinalIgnoreCase))
                        {
                            rushIndex = i;
                            break;
                        }
                }
                if (rushIndex >= 0)
                {
                    SetMember(RoundManager.Instance, "enemyRushIndex", rushIndex);
                    RoundManager.Instance.currentMaxInsidePower = 30f;
                    if (catalogIndex >= 0)
                        SpawnEnemyManaged(catalogIndex | (1 << 16));
                }
                else if (HUDManager.Instance != null)
                    HUDManager.Instance.DisplayTip(
                        "Kue", "This moon has no Hoarding bug in its indoor spawn table");
            }
            else if (action == 25)
            {
                UnityEngine.Object elevator = FindOne("MineshaftElevatorController");
                InvokeAny(elevator, "PressElevatorButtonServerRpc");
            }
            else if (action == 26 && StartOfRound.Instance != null)
                StartOfRound.Instance.SetMagnetOnServerRpc(!StartOfRound.Instance.magnetOn);
            else if (action == 27)
                ShootAllShotgunsManaged();
            else if (action == 28)
                shotgunSpamEnabled = !shotgunSpamEnabled;
            else if (action == 29)
                foreach (UnityEngine.Object vehicle in FindAll("VehicleController"))
                    InvokeAny(vehicle, "DestroyCarServerRpc", -1);
            else if (action == 30)
                slideTaunt = !slideTaunt;
            else if (action == 31)
                ExplodeAllJetpacksManaged(false);
            else if (action == 32)
                explodeJetpacksOnGrab = !explodeJetpacksOnGrab;
            else if (action == 33)
            {
                UnityEngine.Object bridge = FindOne("BridgeTrigger");
                if (TakeOwnership(bridge))
                    InvokeAny(bridge, "BridgeFallServerRpc");
            }
            else if (action == 34)
            {
                UnityEngine.Object bridge = FindOne("BridgeTriggerType2");
                for (int i = 0; i < 4; i++)
                    InvokeAny(bridge, "AddToBridgeInstabilityServerRpc");
            }
            else if (action == 35)
                terminalSpamEnabled = !terminalSpamEnabled;
            else if (action == 36)
                depositSpamEnabled = !depositSpamEnabled;
            else if (action == 37)
                ToggleHorn(true);
            else if (action == 38)
            {
                pjManSpamInterval = Mathf.Clamp(payload / 1000f, 0f, 1f);
                pjManSpam = !pjManSpam;
                nextPjManPulse = 0f;
            }
        }

        private void ProcessManagedActions()
        {
            int targetClientId;
            int action;
            while ((action = NativeBridge.PollAction(out targetClientId)) != 0)
            {
                StartOfRound round = StartOfRound.Instance;
                if (round == null || round.allPlayerScripts == null)
                    continue;
                if (action == 2)
                {
                    try
                    {
                        KillPlayerManaged(PlayerByClientId(targetClientId));
                    }
                    catch (Exception e)
                    {
                        Debug.LogError("[Kue] Kill failed: " + e);
                    }
                }
                else if (action == 4 || action == 5)
                {
                    PlayerControllerB local = LocalPlayer();

                    foreach (PlayerControllerB player in round.allPlayerScripts)
                    {
                        if (!RealPlayer(player))
                            continue;
                        if (player == local)
                            continue;
                        try
                        {
                            KillPlayerManaged(player);
                        }
                        catch (Exception e)
                        {
                            Debug.LogError("[Kue] Remote kill failed: " + e);
                        }
                    }
                    if (action == 4)
                        QueueLocalKill(local);
                }
                else if (action == 6)
                {
                    PlayerControllerB target = PlayerByClientId(targetClientId);
                    persistentOwnedEnemies.Clear();
                    int count = LureEnemiesManaged(target, true);
                    Debug.Log("[Kue] One-shot lure targeted " + targetClientId + " for " + count +
                              " enemies");
                }
                else if (action == 7)
                {
                    try
                    {
                        TeleportAllEnemiesManaged(PlayerByClientId(targetClientId));
                    }
                    catch (Exception e)
                    {
                        Debug.LogError("[Kue] Teleport all enemies failed: " + e);
                    }
                }
                else if (action == 8)
                {
                    if (persistentLureClientId == targetClientId)
                    {
                        persistentLureClientId = -1;
                        persistentOwnedEnemies.Clear();
                        persistentActivatedEnemies.Clear();
                        persistentEnemies.Clear();
                    }
                    else
                    {
                        persistentLureClientId = targetClientId;
                        persistentOwnedEnemies.Clear();
                        persistentActivatedEnemies.Clear();
                        persistentEnemies.Clear();
                        nextPersistentEnemyRefresh = 0f;
                    }
                    NativeBridge.ReportPersistentLureTarget(persistentLureClientId);
                    Debug.Log("[Kue] Persistent lure target=" + persistentLureClientId);
                }
                else if (action == 9)
                {
                    try
                    {
                        SpawnEnemyManaged(targetClientId);
                    }
                    catch (Exception e)
                    {
                        Debug.LogError("[Kue] Enemy spawn failed: " + e);
                    }
                }
                else if (action == 10)
                    KillAllEnemiesManaged();
                else if (action == 11)
                    StunAllEnemiesManaged();
                else if (action == 12)
                    SetFly(!flyEnabled);
                else if (action == 39)
                {
                    try
                    {
                        SpawnEnemyAtPlayerManaged(targetClientId);
                    }
                    catch (Exception e)
                    {
                        Debug.LogError("[Kue] Targeted enemy spawn failed: " + e);
                    }
                }
                else if (action == 40)
                {
                    try
                    {
                        SpawnItemAtPlayerManaged(targetClientId);
                    }
                    catch (Exception e)
                    {
                        Debug.LogError("[Kue] Item spawn failed: " + e);
                    }
                }
                else if (action == 41)
                {
                    try
                    {
                        TeleportItemsToPlayerManaged(PlayerByClientId(targetClientId));
                    }
                    catch (Exception e)
                    {
                        Debug.LogError("[Kue] Item teleport failed: " + e);
                    }
                }
                else if (action == 42)
                {
                    try
                    {
                        SummonEnemyTypeAtPlayerManaged(targetClientId);
                    }
                    catch (Exception e)
                    {
                        Debug.LogError("[Kue] Selected enemy summon failed: " + e);
                    }
                }
                else if (action == 43)
                {
                    try
                    {
                        SetPlayerInsanityManaged(PlayerByClientId(targetClientId), false);
                    }
                    catch (Exception e)
                    {
                        Debug.LogError("[Kue] Clear insanity failed: " + e);
                    }
                }
                else if (action == 44)
                {
                    try
                    {
                        SetPlayerInsanityManaged(PlayerByClientId(targetClientId), true);
                    }
                    catch (Exception e)
                    {
                        Debug.LogError("[Kue] Max insanity failed: " + e);
                    }
                }
                else if (action >= 13)
                {
                    try
                    {
                        ProcessTrollAction(action, targetClientId);
                    }
                    catch (Exception e)
                    {
                        Debug.LogError("[Kue] Troll action failed: " + e);
                    }
                }
            }
        }

        private void UpdateDeathNotifications()
        {
            StartOfRound round = StartOfRound.Instance;
            if (round == null || round.allPlayerScripts == null)
            {
                playerDeathState.Clear();
                return;
            }
            foreach (PlayerControllerB player in round.allPlayerScripts)
            {
                if (!RealPlayer(player))
                    continue;
                ulong id = player.playerClientId;
                bool previous;
                if (!playerDeathState.TryGetValue(id, out previous))
                {
                    playerDeathState[id] = player.isPlayerDead;
                    continue;
                }
                if (!previous && player.isPlayerDead && Flag(12))
                {
                    string name = string.IsNullOrEmpty(player.playerUsername)
                                      ? "Player " + id
                                      : player.playerUsername;
                    if (HUDManager.Instance != null)
                        HUDManager.Instance.DisplayTip("Kue", name + " has died");
                    Debug.Log("[Kue] Death notification: " + name);
                }
                playerDeathState[id] = player.isPlayerDead;
            }
        }

        private void UpdateOutlineColors()
        {
            for (int i = 0; i < outlineColors.Length; i++)
            {
                float r, g, b, a;
                NativeBridge.GetEspColor(i, out r, out g, out b, out a);
                outlineColors[i] = new Color(r, g, b, Mathf.Min(a, 0.24f));
            }
        }

        private Color ItemColor(int value)
        {
            if (!Flag(11))
                return outlineColors[1];
            if (value >= 100)
                return ItemTierColors[4];
            if (value >= 75)
                return ItemTierColors[3];
            if (value >= 50)
                return ItemTierColors[2];
            if (value >= 30)
                return ItemTierColors[1];
            return ItemTierColors[0];
        }

        private void Add(string text, Transform marker, Component rendererRoot, Color color,
                         bool enabled, int value = -1, bool portal = false,
                         float rendererRadius = 12f, MarkKind kind = MarkKind.Static)
        {
            if (marker == null || rendererRoot == null)
                return;
            Mark mark = NextMark();
            if (mark.playerRenderers != null)
                Array.Clear(mark.playerRenderers, 0, mark.playerRenderers.Length);
            mark.name = text;
            mark.kind = kind;
            mark.owner = rendererRoot;
            mark.marker = marker;
            mark.renderers = CachedRenderers(rendererRoot);
            mark.color = color;
            mark.value = value;
            mark.enabled = enabled;
            mark.portal = portal;
            mark.rendererRadius = rendererRadius;
        }

        private void AddPlayer(PlayerControllerB player, Color color, bool enabled)
        {
            if (player == null || player.transform == null)
                return;
            Mark mark = NextMark();
            if (mark.playerRenderers == null)
                mark.playerRenderers = new Renderer[3];
            mark.playerRenderers[0] = player.thisPlayerModel;
            mark.playerRenderers[1] = player.thisPlayerModelLOD1;
            mark.playerRenderers[2] = player.thisPlayerModelLOD2;
            mark.renderers = mark.playerRenderers;
            mark.name = player.playerUsername;
            mark.kind = MarkKind.Player;
            mark.owner = player;
            mark.marker = player.playerGlobalHead != null ? player.playerGlobalHead
                                                          : player.transform;
            mark.color = color;
            mark.value = -1;
            mark.enabled = enabled;
            mark.portal = false;
            mark.rendererRadius = 7f;
        }

        private Renderer[] CachedRenderers(Component root)
        {
            int instanceId = root.GetInstanceID();
            activeRendererRoots.Add(instanceId);
            RendererCacheEntry entry;
            if (rendererCache.TryGetValue(instanceId, out entry) && entry.owner == root)
                return entry.renderers;
            entry =
                new RendererCacheEntry { owner = root,
                                         renderers = root.GetComponentsInChildren<Renderer>(true) };
            rendererCache[instanceId] = entry;
            return entry.renderers;
        }

        private void PruneRendererCaches()
        {
            staleCacheKeys.Clear();
            foreach (KeyValuePair<int, RendererCacheEntry> pair in rendererCache)
                if (pair.Value.owner == null || !activeRendererRoots.Contains(pair.Key))
                    staleCacheKeys.Add(pair.Key);
            foreach (int key in staleCacheKeys)
                rendererCache.Remove(key);

            staleCacheKeys.Clear();
            foreach (KeyValuePair<int, MeshCacheEntry> pair in meshCache)
                if (pair.Value.owner == null)
                    staleCacheKeys.Add(pair.Key);
            foreach (int key in staleCacheKeys)
                meshCache.Remove(key);
        }

        private Mark NextMark()
        {
            if (markCount == marks.Count)
                marks.Add(new Mark());
            return marks[markCount++];
        }

        private void RebuildMarks()
        {
            markCount = 0;
            activeRendererRoots.Clear();
            PlayerControllerB local = LocalPlayer();
            StartOfRound round = StartOfRound.Instance;

            if (round != null && round.allPlayerScripts != null)
                foreach (PlayerControllerB player in round.allPlayerScripts)
                    if (RealPlayer(player) && player != local && !player.isPlayerDead)
                        AddPlayer(player, outlineColors[0], Flag(0));

            if (activeEnemyCounts.Length != enemyCatalog.Count)
                activeEnemyCounts = new int[enemyCatalog.Count];
            else
                Array.Clear(activeEnemyCounts, 0, activeEnemyCounts.Length);
            foreach (UnityEngine.Object loaded in FindAll("EnemyAI"))
            {
                EnemyAI enemy = loaded as EnemyAI;
                if (enemy != null && !enemy.isEnemyDead)
                {
                    KillHivelessBees(enemy, local);
                    Add(enemy.enemyType != null ? enemy.enemyType.enemyName : enemy.GetType().Name,
                        enemy.transform, enemy, outlineColors[2], Flag(2), -1, false, 12f,
                        MarkKind.Enemy);
                    if (enemy.enemyType != null)
                    {
                        int catalogIndex;
                        if (!enemyCatalogIndices.TryGetValue(enemy.enemyType, out catalogIndex) &&
                            !enemyCatalogNames.TryGetValue(enemy.enemyType.enemyName,
                                                           out catalogIndex))
                            catalogIndex = -1;
                        if (catalogIndex >= 0)
                            activeEnemyCounts[catalogIndex]++;
                    }
                }
            }
            bool activeCountsChanged = reportedEnemyCounts.Length != activeEnemyCounts.Length;
            if (!activeCountsChanged)
                for (int i = 0; i < activeEnemyCounts.Length; i++)
                    if (activeEnemyCounts[i] != reportedEnemyCounts[i])
                    {
                        activeCountsChanged = true;
                        break;
            }
            if (activeCountsChanged)
            {
                EnemyActivityBeginResult begin = NativeBridge.BeginActiveEnemyTypes();
                if (begin != EnemyActivityBeginResult.Begun)
                {
                    Debug.LogError("[Kue] Active enemy catalog begin failed: " + begin);
                    return;
                }
                for (int i = 0; i < activeEnemyCounts.Length; i++)
                    if (activeEnemyCounts[i] > 0)
                    {
                        EnemyActivityReportResult report =
                            NativeBridge.ReportActiveEnemyType(i, activeEnemyCounts[i]);
                        if (report != EnemyActivityReportResult.Recorded)
                        {
                            NativeBridge.AbortActiveEnemyTypes();
                            Debug.LogError("[Kue] Active enemy catalog report failed at " + i +
                                           ": " + report);
                            return;
                        }
                    }
                EnemyActivityCommitResult commit = NativeBridge.CommitActiveEnemyTypes();
                if (commit != EnemyActivityCommitResult.Changed &&
                    commit != EnemyActivityCommitResult.Unchanged)
                {
                    Debug.LogError("[Kue] Active enemy catalog commit failed: " + commit);
                    return;
                }
                if (reportedEnemyCounts.Length != activeEnemyCounts.Length)
                    reportedEnemyCounts = new int[activeEnemyCounts.Length];
                Array.Copy(activeEnemyCounts, reportedEnemyCounts, activeEnemyCounts.Length);
            }

            foreach (UnityEngine.Object loaded in FindAll("GrabbableObject"))
            {
                GrabbableObject item = loaded as GrabbableObject;
                if (item != null && item.itemProperties != null && item.itemProperties.isScrap &&
                    !item.isHeld && !item.isPocketed && !item.deactivated)
                    Add(item.itemProperties.itemName, item.transform, item,
                        ItemColor(item.scrapValue), Flag(1), item.scrapValue, false, 5f,
                        MarkKind.Item);
            }

            foreach (UnityEngine.Object loaded in FindAll("EntranceTeleport"))
            {
                EntranceTeleport entrance = loaded as EntranceTeleport;
                if (entrance == null || entrance.entrancePoint == null)
                    continue;
                bool fire = entrance.entranceId != 0;
                Add(fire ? "Fire Exit" : "Main Entrance", entrance.entrancePoint, entrance,
                    outlineColors[fire ? 4 : 3], Flag(fire ? 4 : 3), -1, true);
            }

            HangarShipDoor shipDoor = UnityEngine.Object.FindObjectOfType<HangarShipDoor>();
            if (shipDoor != null)
            {
                Component shipRoot = shipDoor;
                if (local != null && !local.isInHangarShipRoom && round != null &&
                    round.shipBounds != null && round.shipBounds.transform.parent != null)
                    shipRoot = round.shipBounds.transform.parent;
                Add("Ship", shipDoor.transform, shipRoot, outlineColors[5], Flag(5), -1, false,
                    100f);
            }
            else if (round != null && round.shipBounds != null)
            {
                Transform root = round.shipBounds.transform.parent != null
                                     ? round.shipBounds.transform.parent
                                     : round.shipBounds.transform;
                Add("Ship", round.shipBounds.transform, root, outlineColors[5], Flag(5), -1, false,
                    100f);
            }
            for (int i = markCount; i < marks.Count; i++)
            {
                marks[i].marker = null;
                marks[i].owner = null;
                marks[i].renderers = null;
                if (marks[i].playerRenderers != null)
                    Array.Clear(marks[i].playerRenderers, 0, marks[i].playerRenderers.Length);
                marks[i].enabled = false;
            }
            PruneRendererCaches();
        }

        private Camera GameCamera()
        {
            PlayerControllerB local = LocalPlayer();
            StartOfRound round = StartOfRound.Instance;
            if (local != null && local.isPlayerDead && round != null &&
                round.spectateCamera != null && round.spectateCamera.enabled &&
                round.spectateCamera.gameObject.activeInHierarchy)
            {
                activeCamera = round.spectateCamera;
                return activeCamera;
            }
            if (local != null && local.gameplayCamera != null && local.gameplayCamera.enabled)
            {
                activeCamera = local.gameplayCamera;
                return activeCamera;
            }
            if (activeCamera != null && activeCamera.enabled &&
                activeCamera.gameObject.activeInHierarchy)
                return activeCamera;
            Camera main = Camera.main;
            if (main != null && main.enabled)
            {
                activeCamera = main;
                return activeCamera;
            }
            int cameraCount = Camera.allCamerasCount;
            if (cameraBuffer.Length < cameraCount)
                cameraBuffer = new Camera[cameraCount];
            cameraCount = Camera.GetAllCameras(cameraBuffer);
            for (int i = 0; i < cameraCount; i++)
            {
                Camera camera = cameraBuffer[i];
                if (camera != null && camera.enabled && camera.gameObject.activeInHierarchy)
                {
                    activeCamera = camera;
                    return activeCamera;
                }
            }
            activeCamera = null;
            return null;
        }

        private void EnsureStyles()
        {
            if (label != null)
                return;
            label =
                new GUIStyle(GUI.skin.label) { alignment = TextAnchor.MiddleCenter, fontSize = 13 };
            label.normal.textColor = Color.white;
            shadow = new GUIStyle(label);
            shadow.normal.textColor = new Color(0f, 0f, 0f, 0.9f);
        }

        private void OnGUI()
        {
            if (Event.current == null || Event.current.type != EventType.Repaint ||
                lastGuiFrame == Time.frameCount)
                return;
            lastGuiFrame = Time.frameCount;
            EnsureStyles();
            if (menuOpen)
                RenderImGuiMenu();
            DrawLabels();
            if (menuOpen && menuTexture != null)
                GUI.DrawTexture(menuRect, menuTexture);
        }

        private void DrawLabels()
        {
            if ((espFlags & 0x3f) == 0)
                return;
            Camera camera = GameCamera();
            if (camera == null)
                return;
            bool drawOutlines = Flag(9) && highlightVolume == null;
            bool drawPortalOutlines = Flag(9);
            bool drawLines = Flag(10);
            bool showNames = Flag(6);
            bool showValues = Flag(7);
            bool showDistance = Flag(8);
            Transform cameraTransform = camera.transform;
            Vector3 cameraPosition = cameraTransform.position;
            Vector3 cameraForward = cameraTransform.forward;
            PlayerControllerB local = LocalPlayer();
            Vector3 origin = local != null ? local.transform.position : cameraPosition;
            float maximumDistanceSquared = maxDistance * maxDistance;
            float screenWidth = Screen.width;
            float screenHeight = Screen.height;
            int labelFlags = espFlags & ((1 << 6) | (1 << 7) | (1 << 8));
            for (int markIndex = 0; markIndex < markCount; markIndex++)
            {
                Mark mark = marks[markIndex];
                if (!mark.enabled || mark.marker == null || !MarkStillVisible(mark))
                    continue;
                Vector3 world = mark.marker.position;
                float distanceSquared = (origin - world).sqrMagnitude;
                if (distanceSquared > maximumDistanceSquared)
                    continue;
                Vector3 toTarget = world - cameraPosition;
                if (Vector3.Dot(cameraForward, toTarget) <= 0.01f)
                    continue;
                Color drawColor = mark.color;
                drawColor.a = 1f;
                if (mark.portal)
                {
                    if (drawPortalOutlines)
                        DrawPortalOutline(camera, mark, drawColor);
                }
                else if (drawOutlines)
                    DrawMeshSilhouette(camera, mark, drawColor);
                Vector3 labelWorld = mark.portal ? world + Vector3.up * 2.9f : world;
                Vector3 viewport = camera.WorldToViewportPoint(labelWorld);
                if (viewport.z <= 0.01f || viewport.x < 0f || viewport.x > 1f || viewport.y < 0f ||
                    viewport.y > 1f)
                    continue;
                float x = viewport.x * screenWidth;
                float y = (1f - viewport.y) * screenHeight;
                if (drawLines)
                    DrawLine(new Vector2(screenWidth * 0.5f, screenHeight), new Vector2(x, y),
                             drawColor, 1.5f);
                int roundedDistance =
                    showDistance ? Mathf.RoundToInt(Mathf.Sqrt(distanceSquared)) : -1;
                if (mark.labelFlags != labelFlags || mark.labelName != mark.name ||
                    mark.labelValue != mark.value || mark.labelDistance != roundedDistance)
                {
                    string builtText = showNames ? mark.name : "";
                    if (showValues && mark.value >= 0)
                        builtText += (builtText.Length > 0 ? "  " : "") + "$" + mark.value;
                    if (showDistance)
                        builtText += (builtText.Length > 0 ? "\n" : "") + roundedDistance + "m";
                    mark.labelText = builtText;
                    mark.labelFlags = labelFlags;
                    mark.labelName = mark.name;
                    mark.labelValue = mark.value;
                    mark.labelDistance = roundedDistance;
                }
                string text = mark.labelText;
                if (text.Length == 0)
                    continue;
                Rect rect = new Rect(x - 140f, y - 40f, 280f, 38f);
                GUI.Label(new Rect(rect.x + 1, rect.y + 1, rect.width, rect.height), text, shadow);
                Color previousColor = GUI.color;
                GUI.color = drawColor;
                GUI.Label(rect, text, label);
                GUI.color = previousColor;
            }
        }

        private static bool MarkStillVisible(Mark mark)
        {
            if (mark.owner == null)
                return mark.kind == MarkKind.Static;
            switch (mark.kind)
            {
            case MarkKind.Player:
                PlayerControllerB player = (PlayerControllerB)mark.owner;
                return !player.isPlayerDead && player.isPlayerControlled;
            case MarkKind.Enemy:
                return !((EnemyAI)mark.owner).isEnemyDead;
            case MarkKind.Item:
                GrabbableObject item = (GrabbableObject)mark.owner;
                return !item.isHeld && !item.isPocketed && !item.deactivated;
            default:
                return true;
            }
        }

        private Vector3[] StaticMeshVertices(Renderer renderer)
        {
            int instanceId = renderer.GetInstanceID();
            MeshCacheEntry entry;
            if (!meshCache.TryGetValue(instanceId, out entry) || entry.owner != renderer)
            {
                entry = new MeshCacheEntry { owner = renderer,
                                             filter = renderer.GetComponent<MeshFilter>() };
                meshCache[instanceId] = entry;
            }
            Mesh mesh = entry.filter != null ? entry.filter.sharedMesh : null;
            if (mesh == null || !mesh.isReadable)
                return null;
            if (entry.mesh != mesh || entry.vertices == null)
            {
                entry.mesh = mesh;
                entry.vertices = mesh.vertices;
            }
            return entry.vertices;
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private void ProjectVertex(Vector3 vertex, Matrix4x4 localToWorld, Camera camera,
                                   Vector3 markerPosition, float radiusSquared, bool limitRadius,
                                   float screenWidth, float screenHeight)
        {
            ProjectWorldPoint(localToWorld.MultiplyPoint3x4(vertex), camera, markerPosition,
                              radiusSquared, limitRadius, screenWidth, screenHeight);
        }

        private void ProjectWorldPoint(Vector3 world, Camera camera, Vector3 markerPosition,
                                       float radiusSquared, bool limitRadius, float screenWidth,
                                       float screenHeight)
        {
            if (limitRadius && (world - markerPosition).sqrMagnitude > radiusSquared)
                return;
            Vector3 viewport = camera.WorldToViewportPoint(world);
            if (!Finite(viewport.x) || !Finite(viewport.y) || !Finite(viewport.z) ||
                viewport.z <= 0.01f || viewport.x < -1f || viewport.x > 2f || viewport.y < -1f ||
                viewport.y > 2f)
                return;
            projectedVertices.Add(
                new Vector2(viewport.x * screenWidth, (1f - viewport.y) * screenHeight));
        }

        private void DrawMeshSilhouette(Camera camera, Mark mark, Color color)
        {
            if (mark.renderers == null)
                return;
            projectedVertices.Clear();
            Vector3 markerPosition = mark.marker.position;
            float radiusSquared = mark.rendererRadius * mark.rendererRadius;
            bool limitRadius = mark.rendererRadius > 0f;
            bool skeletonProjected = false;
            float screenWidth = Screen.width;
            float screenHeight = Screen.height;
            foreach (Renderer renderer in mark.renderers)
            {
                if (renderer == null || !renderer.enabled)
                    continue;
                if (!(renderer is MeshRenderer) && !(renderer is SkinnedMeshRenderer))
                    continue;
                Bounds bounds = renderer.bounds;
                if (limitRadius && (bounds.extents.sqrMagnitude > radiusSquared ||
                                    (bounds.center - markerPosition).sqrMagnitude > radiusSquared))
                    continue;
                Matrix4x4 localToWorld = renderer.localToWorldMatrix;
                SkinnedMeshRenderer skinned = renderer as SkinnedMeshRenderer;
                if (skinned != null)
                {
                    if (skeletonProjected)
                        continue;
                    Transform[] bones = skinned.bones;
                    if (bones == null || bones.Length == 0)
                        continue;
                    skeletonProjected = true;
                    Transform cameraTransform = camera.transform;
                    Vector3 right = cameraTransform.right * BoneOutlineRadius;
                    Vector3 up = cameraTransform.up * BoneOutlineRadius;
                    for (int i = 0; i < bones.Length; i++)
                    {
                        Transform bone = bones[i];
                        if (bone == null)
                            continue;
                        Vector3 world = bone.position;
                        ProjectWorldPoint(world + right, camera, markerPosition, radiusSquared,
                                          limitRadius, screenWidth, screenHeight);
                        ProjectWorldPoint(world - right, camera, markerPosition, radiusSquared,
                                          limitRadius, screenWidth, screenHeight);
                        ProjectWorldPoint(world + up, camera, markerPosition, radiusSquared,
                                          limitRadius, screenWidth, screenHeight);
                        ProjectWorldPoint(world - up, camera, markerPosition, radiusSquared,
                                          limitRadius, screenWidth, screenHeight);
                    }
                }
                else
                {
                    Vector3[] vertices = StaticMeshVertices(renderer);
                    if (vertices == null || vertices.Length < 3)
                        continue;
                    int step = Mathf.Max(1, vertices.Length / 256);
                    for (int i = 0; i < vertices.Length; i += step)
                        ProjectVertex(vertices[i], localToWorld, camera, markerPosition,
                                      radiusSquared, limitRadius, screenWidth, screenHeight);
                }
            }
            BuildHull(projectedVertices, hullVertices);
            if (hullVertices.Count < 3)
                return;
            for (int i = 0; i < hullVertices.Count; i++)
                DrawLine(hullVertices[i], hullVertices[(i + 1) % hullVertices.Count], color, 2f);
        }

        private void DrawPortalOutline(Camera camera, Mark mark, Color color)
        {
            if (mark.marker == null)
                return;
            Vector3 baseCenter = mark.marker.position + Vector3.up * 0.08f;
            Vector3 right = mark.marker.right.normalized * 1.15f;
            Vector3 up = Vector3.up * 2.65f;
            portalWorldCorners[0] = baseCenter - right;
            portalWorldCorners[1] = baseCenter + right;
            portalWorldCorners[2] = baseCenter + right + up;
            portalWorldCorners[3] = baseCenter - right + up;
            for (int i = 0; i < portalWorldCorners.Length; i++)
            {
                Vector3 viewport = camera.WorldToViewportPoint(portalWorldCorners[i]);
                if (viewport.z <= 0.01f)
                    return;
                portalScreenCorners[i] =
                    new Vector2(viewport.x * Screen.width, (1f - viewport.y) * Screen.height);
            }
            for (int i = 0; i < portalScreenCorners.Length; i++)
                DrawLine(portalScreenCorners[i],
                         portalScreenCorners[(i + 1) % portalScreenCorners.Length], color, 2f);
        }

        private static float Cross(Vector2 origin, Vector2 a, Vector2 b)
        {
            return (a.x - origin.x) * (b.y - origin.y) - (a.y - origin.y) * (b.x - origin.x);
        }

        private static int CompareEnemyTypes(EnemyType left, EnemyType right)
        {
            return string.Compare(left.enemyName, right.enemyName,
                                  StringComparison.OrdinalIgnoreCase);
        }

        private static int CompareItems(Item left, Item right)
        {
            return string.Compare(left.itemName, right.itemName,
                                  StringComparison.OrdinalIgnoreCase);
        }

        private static int CompareScreenPoints(Vector2 left, Vector2 right)
        {
            int x = left.x.CompareTo(right.x);
            return x != 0 ? x : left.y.CompareTo(right.y);
        }

        private static void BuildHull(List<Vector2> points, List<Vector2> hull)
        {
            hull.Clear();
            if (points.Count < 3)
                return;
            points.Sort(ScreenPointComparison);
            for (int i = 0; i < points.Count; i++)
            {
                while (hull.Count >= 2 &&
                       Cross(hull[hull.Count - 2], hull[hull.Count - 1], points[i]) <= 0f)
                    hull.RemoveAt(hull.Count - 1);
                hull.Add(points[i]);
            }
            int lower = hull.Count + 1;
            for (int i = points.Count - 2; i >= 0; i--)
            {
                while (hull.Count >= lower &&
                       Cross(hull[hull.Count - 2], hull[hull.Count - 1], points[i]) <= 0f)
                    hull.RemoveAt(hull.Count - 1);
                hull.Add(points[i]);
            }
            if (hull.Count > 1)
                hull.RemoveAt(hull.Count - 1);
        }

        private void DrawLine(Vector2 start, Vector2 end, Color color, float width)
        {
            if (lineTexture == null || !Finite(start) || !Finite(end) || !Finite(width))
                return;
            Vector2 delta = end - start;
            float length = delta.magnitude;
            if (!Finite(delta) || !Finite(length) || length < 0.1f)
                return;
            Matrix4x4 previous = GUI.matrix;
            Color previousColor = GUI.color;
            float angle = Mathf.Atan2(delta.y, delta.x) * Mathf.Rad2Deg;
            GUIUtility.RotateAroundPivot(angle, start);
            GUI.color = color;
            GUI.DrawTexture(new Rect(start.x, start.y - width * 0.5f, length, width), lineTexture);
            GUI.color = previousColor;
            GUI.matrix = previous;
        }
    }
}
