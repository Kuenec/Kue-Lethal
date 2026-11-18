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
using UnityEngine.InputSystem.Controls;
using UnityEngine.Rendering;
using UnityEngine.Rendering.HighDefinition;
using UnityEngine.SceneManagement;
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

    internal enum EnemyTraitsResult
    {
        Recorded,
        NoCatalog,
        StaleGeneration,
        IndexOutOfRange
    }

    internal enum PlayerAction
    {
        None = 0,
        TeleportTo,
        Kill,
        HealSelf,
        KillAll,
        KillAllExceptLocal,
        LureAllEnemies,
        TeleportAllEnemies,
        TogglePersistentLure,
        SpawnEnemy,
        KillAllEnemies,
        StunAllEnemies,
        ToggleFly,
        ToggleShipHorn,
        ToggleShipLights,
        ToggleFactoryLights,
        BlowUpAllMines,
        ToggleAllMines,
        ToggleAllTurrets,
        BerserkAllTurrets,
        OpenShipDoorSpace,
        ForceTentacleAttack,
        SpawnMaskedEnemy,
        EjectEveryone,
        SpawnHoardingBugInfestation,
        ToggleMineshaftElevator,
        ToggleVehicleMagnet,
        ShootAllShotguns,
        ToggleShotgunSpam,
        ExplodeCruiser,
        SlideTaunt,
        ExplodeAllJetpacks,
        ToggleExplodeJetpacksOnGrab,
        ForceBridgeFall,
        ForceSmallBridgeFall,
        ToggleTerminalSound,
        ToggleDepositDeskSound,
        ToggleCarHorn,
        TogglePjManSpam,
        SpawnEnemyAtPlayer,
        SpawnItemAtPlayer,
        TeleportItemsToPlayer,
        SummonEnemyTypeAtPlayer,
        ClearInsanity,
        MaxInsanity,
        DepositShipScrap,
        AddTerminalCredits,
        ToggleThirdPerson,
        TravelToMoon,
        ToggleHearEveryone,
        SpawnHazardAtPlayer,
    }

    internal enum SessionSwitch
    {
        Fly,
        ThirdPerson,
        EmoteWhileMoving,
        HearEveryone,
        ShipHorn,
        ShipLights,
        TerminalSound,
        ShipDoorButtons,
        CruiserMagnet,
        PlushieSpam,
        CruiserHorn,
        DeskDoorSound,
        LandminesArmed,
        TurretsActive,
        FacilityPower,
        ShotgunSpam,
        JetpackTrap,
    }

    internal enum WorldFeature
    {
        Host,
        ShipInOrbit,
        MoonLoaded,
        PlayersCanDie,
        EnemiesAlive,
        ShipHorn,
        ShipLights,
        Terminal,
        Plushie,
        Cruiser,
        CompanyDesk,
        Landmines,
        Turrets,
        FacilityPower,
        MainBridge,
        SmallBridge,
        MineshaftElevator,
        Shotguns,
        Jetpacks,
        HauntedMasks,
        HoardingBugNest,
        ShipDoor,
        ModelPreviews,
    }

    internal static class NativeBridge
    {
        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern IntPtr RenderMenu(int screenWidth, int screenHeight, float mouseX,
                                                 float mouseY, int mouseDown, float wheel,
                                                 int pressedKey, out int x, out int y,
                                                 out int width, out int height, out int byteCount,
                                                 out ulong pixelRevision);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void GetHudConfig(out int flags, out float maxDistance,
                                                 out int menuKey, out int tickIntervalMs,
                                                 out int revision);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void GetEspColor(int category, out float r, out float g, out float b,
                                                out float a);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern PlayerAction PollAction(out int clientId, out int catalogIndex,
                                                       out int count, out int amount);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void ResetRuntimeCatalogs();

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void ReportSessionState(int switches, int world,
                                                       long playersInFacility, int lureClientId,
                                                       int lureSecondsLeft);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern EnemyTraitsResult ReportEnemySpawnTraits(int index, int outside,
                                                                        int supported);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern int GetPreviewRequest(out int kind, out int catalogIndex, out int x,
                                                     out int y, out int width, out int height);

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

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern CatalogBeginResult BeginHazardCatalog();

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern CatalogReportResult ReportHazardType(int instanceId, string name);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern CatalogCommitResult CommitHazardCatalog();

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void AbortHazardCatalog();
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

        private sealed class PreviewPart
        {
            public Mesh mesh;
            public bool ownsMesh;
            public Matrix4x4 matrix;
            public Material[] materials;
        }

        private sealed class HuntedEnemy
        {
            public EnemyAI enemy;
            public float nextOwnershipRequest;
            public float nextAggravation;
            public float engagedAt;
            public bool engaged;
        }

        private const float MinimumLocalDeathRoundTime = 2f;
        private const int LethalDamage = 1000;
        private const int FullHealth = 100;
        private const int MaximumSpawnCount = 20;
        private const float MinimumEnemySpawnDistance = 2f;
        private const float StunSeconds = 60f;
        private const float LureSeconds = 30f;
        private const float EngageDistance = 7f;
        private const float RetakeDistance = 22f;
        private const float HuntSpeed = 13f;
        private const float HuntDoorSpeed = 8f;
        private const float OwnershipRetrySeconds = 0.75f;
        private const float AggravateRetrySeconds = 2f;
        private const int PreviewKindEnemy = 0;
        private const int PreviewKindItem = 1;
        private const int PreviewKindTrap = 2;
        private const float PreviewFieldOfView = 30f;
        private const float PreviewSpinDegrees = 35f;

        private readonly List<Mark> marks = new List<Mark>();
        private readonly List<Vector2> projectedVertices = new List<Vector2>(1024);
        private readonly List<Vector2> hullVertices = new List<Vector2>(1024);
        private readonly Vector3[] portalWorldCorners = new Vector3[4];
        private readonly Vector2[] portalScreenCorners = new Vector2[4];
        private readonly List<EnemyType> enemyCatalogBuffer = new List<EnemyType>();
        private readonly List<Item> itemCatalogBuffer = new List<Item>();
        private readonly List<SelectableLevel> moonCatalogBuffer = new List<SelectableLevel>();
        private readonly List<int> moonLevelBuffer = new List<int>();
        private readonly List<bool> moonSceneBuffer = new List<bool>();
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
        private float maxDistance = 1000f;
        private int lastGuiFrame = -1;
        private readonly Dictionary<ulong, bool> playerDeathState = new Dictionary<ulong, bool>();
        private readonly Dictionary<int, HuntedEnemy> hunted = new Dictionary<int, HuntedEnemy>();
        private readonly List<HuntedEnemy> huntedOrder = new List<HuntedEnemy>();
        private int lureClientId = -1;
        private bool lureRelentless;
        private float lureEndsAt;
        private float nextLureScan;
        private readonly List<GameObject> hazardCatalog = new List<GameObject>();
        private readonly List<GameObject> hazardCatalogBuffer = new List<GameObject>();
        private readonly List<string> hazardNameBuffer = new List<string>();
        private bool hazardCatalogReady;
        private int hazardCatalogSignature;
        private int[] reportedEnemyTraits = new int[0];
        private float nextEnemyTraitsRefresh;
        private int livingEnemyCount;
        private int scannedWorld;
        private int scannedSwitches;
        private long scannedPlayersInFacility;
        private float nextWorldScan;
        private bool hearEveryone;
        private readonly List<PreviewPart> previewParts = new List<PreviewPart>();
        private GameObject previewHolder;
        private GameObject previewPrefab;
        private RenderTexture previewTexture;
        private CommandBuffer previewCommands;
        private Material previewDepthMaterial;
        private Shader previewTextureShader;
        private Bounds previewBounds;
        private Rect previewRect;
        private bool previewVisible;
        private bool previewUnavailable;
        private readonly List<EnemyType> enemyCatalog = new List<EnemyType>();
        private readonly List<Item> itemCatalog = new List<Item>();
        private readonly List<SelectableLevel> moonCatalog = new List<SelectableLevel>();
        private readonly List<int> moonLevelIndices = new List<int>();
        private readonly List<bool> moonHasScene = new List<bool>();
        private int swappedLevelSlot = -1;
        private SelectableLevel swappedOriginalLevel;
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
        private static readonly MethodInfo ThrowObjectServerRpcMethod =
            typeof(PlayerControllerB)
                .GetMethod("ThrowObjectServerRpc", BindingFlags.Instance | BindingFlags.NonPublic);
        private static readonly string[] HazardNameSuffixes = { "Hazard", "Container", "Type" };
        private static readonly string[] HelperLayerNames = { "ScanNode", "MapRadar" };
        private static readonly int GuiDepthTestId = Shader.PropertyToID("unity_GUIZTestMode");
        private static readonly Color PreviewBackground = new Color(0.047f, 0.047f, 0.055f, 1f);
        private static readonly Color PreviewUntexturedColor = new Color(0.32f, 0.32f, 0.35f, 1f);
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
            if (hearEveryone)
                SetHearEveryone(false);
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
            ReleasePreview();
            if (previewDepthMaterial != null)
                Destroy(previewDepthMaterial);
            if (previewCommands != null)
                previewCommands.Release();
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
                {
                    nextWorldScan = 0f;
                    nextEnemyTraitsRefresh = 0f;
                    CaptureMenu();
                }
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
            RefreshHazardCatalog();
            RefreshEnemyTraits();
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
            ReportSession();
        }

        private void LateUpdate()
        {
            ApplyLocalPlayerFeatures();
            UpdateLure();
            UpdateHearEveryone();
            UpdatePreview();
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
            int visibleLayers = PlayerVisibleLayers(camera);
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
                    if (!ModelRenderer(renderer, visibleLayers))
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
            IntPtr pixels = NativeBridge.RenderMenu(Screen.width, Screen.height, mx, my, down,
                                                    wheel, PressedKey(), out x, out y, out width,
                                                    out height, out bytes, out pixelRevision);
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

        private static int PressedKey()
        {
            Keyboard keyboard = Keyboard.current;
            if (keyboard == null || !keyboard.anyKey.wasPressedThisFrame)
                return 0;
            foreach (KeyControl key in keyboard.allKeys)
                if (key != null && key.wasPressedThisFrame)
                    return (int)key.keyCode;
            return 0;
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
            player.DamagePlayerFromOtherClientServerRpc(LethalDamage, Vector3.zero,
                                                        (int)local.playerClientId);
            StartCoroutine(ConfirmRemoteKill(player));
            Debug.Log("[Kue] Lethal damage sent to " + player.playerUsername + " (" +
                      player.playerClientId + ")");
        }

        private IEnumerator ConfirmRemoteKill(PlayerControllerB player)
        {
            yield return new WaitForSeconds(1.5f);
            if (player != null && !player.isPlayerDead && player.isPlayerControlled)
                ReportActionFailure(player.playerUsername +
                                    " survived: the game blocks damage in orbit");
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
            ShowTip(message);
        }

        private static void ShowTip(string message)
        {
            HUDManager hud = HUDManager.Instance;
            if (hud != null)
                hud.DisplayTip("Solar", message);
        }

        private void TeleportToPlayerManaged(PlayerControllerB target)
        {
            PlayerControllerB local = LocalPlayer();
            if (target == null || local == null || target == local)
            {
                ReportActionFailure("Teleport failed: target unavailable");
                return;
            }
            if (local.isPlayerDead)
            {
                ReportActionFailure("Teleport failed: you are dead");
                return;
            }
            Vector3 position = TargetPosition(target);
            local.isInElevator = target.isInElevator;
            local.isInHangarShipRoom = target.isInHangarShipRoom;
            local.isInsideFactory = target.isInsideFactory;
            local.TeleportPlayer(position, false, 0f, false, !flyEnabled);
            Debug.Log("[Kue] Teleported to " + target.playerUsername + " at " + position);
        }

        private void HealSelfManaged()
        {
            PlayerControllerB local = LocalPlayer();
            if (local == null || local.isPlayerDead)
            {
                ReportActionFailure("Heal failed: you are not alive");
                return;
            }
            local.health = FullHealth;
            local.MakeCriticallyInjured(false);
            if (HUDManager.Instance != null)
                HUDManager.Instance.UpdateHealthUI(FullHealth, false);
            Debug.Log("[Kue] Healed the local player");
        }

        private static bool EnemyOwnedBy(EnemyAI enemy, ulong clientId)
        {
            return enemy != null && enemy.thisNetworkObject != null &&
                   enemy.thisNetworkObject.OwnerClientId == clientId;
        }

        private static bool SharesArea(EnemyAI enemy, PlayerControllerB player)
        {
            return enemy.isOutside != player.isInsideFactory;
        }

        private static int ChaseState(EnemyAI enemy)
        {
            switch (enemy.GetType().Name)
            {
            case "SpringManAI":
            case "MaskedPlayerEnemy":
            case "ForestGiantAI":
            case "RadMechAI":
                return 1;
            case "FlowermanAI":
            case "JesterAI":
            case "HoarderBugAI":
            case "SandSpiderAI":
            case "CentipedeAI":
            case "ButlerEnemyAI":
            case "MouthDogAI":
                return 2;
            default:
                return -1;
            }
        }

        private static void AggravateEnemy(EnemyAI enemy, PlayerControllerB target, bool owned)
        {
            string type = enemy.GetType().Name;
            int targetId = (int)target.playerClientId;
            if (type == "CrawlerAI")
            {
                if (enemy.currentBehaviourStateIndex != 1)
                    InvokeAny(enemy, "BeginChasingPlayerServerRpc", targetId);
                return;
            }
            if (type == "NutcrackerEnemyAI")
            {
                if (enemy.currentBehaviourStateIndex != 2)
                    InvokeAny(enemy, "SeeMovingThreatServerRpc", targetId, true);
                return;
            }
            if (owned && type == "BaboonBirdAI")
                InvokeAny(enemy, "SetAggressiveModeServerRpc", 1);
            if (owned && type == "MouthDogAI")
                InvokeAny(enemy, "ReactToOtherDogHowl", target.transform.position);
            int state = ChaseState(enemy);
            if (state < 0 || enemy.enemyBehaviourStates == null ||
                state >= enemy.enemyBehaviourStates.Length ||
                enemy.currentBehaviourStateIndex == state)
                return;
            if (owned)
                enemy.SwitchToBehaviourState(state);
            else
                enemy.SwitchToBehaviourServerRpc(state);
            if (owned && type == "FlowermanAI")
                InvokeAny(enemy, "EnterAngerModeServerRpc", 20f);
        }

        private static void SteerEnemy(EnemyAI enemy, Vector3 goal, float distance, bool hunting)
        {
            NavMeshAgent agent = enemy.agent;
            if (agent == null || !agent.enabled || !agent.isOnNavMesh || enemy.inSpecialAnimation ||
                enemy.stunNormalizedTimer > 0f)
                return;
            enemy.destination = goal;
            enemy.moveTowardsDestination = true;
            if ((agent.destination - goal).sqrMagnitude > 1f)
                agent.SetDestination(goal);
            if (!hunting || agent.speed <= 0.1f || distance <= EngageDistance)
                return;
            Vector3 heading = agent.desiredVelocity;
            heading.y = 0f;
            float extraSpeed = HuntSpeed - agent.speed;
            if (heading.sqrMagnitude < 0.01f || extraSpeed <= 0f)
                return;
            agent.Move(heading.normalized * (extraSpeed * Time.deltaTime));
        }

        private void ToggleLure(int clientId, bool relentless)
        {
            if (lureClientId == clientId && lureRelentless == relentless)
            {
                StopLure();
                ShowTip(relentless ? "Persistent targeting off" : "Lure stopped");
                return;
            }
            PlayerControllerB target = PlayerByClientId(clientId);
            if (!RealPlayer(target) || target.isPlayerDead)
            {
                ReportActionFailure("Lure failed: that player is not alive");
                return;
            }
            StopLure();
            lureClientId = clientId;
            lureRelentless = relentless;
            lureEndsAt = relentless ? float.PositiveInfinity : Time.unscaledTime + LureSeconds;
            nextLureScan = 0f;
            nextLureNoise = 0f;
            Debug.Log("[Kue] " + (relentless ? "Persistent targeting" : "Timed lure") + " on " +
                      target.playerUsername + " (" + clientId + ")");
            ShowTip(relentless ? "Enemies now hunt " + target.playerUsername
                               : "Luring enemies to " + target.playerUsername + " for 30 s");
        }

        private void StopLure()
        {
            lureClientId = -1;
            hunted.Clear();
            huntedOrder.Clear();
        }

        private void RefreshHunted(PlayerControllerB target)
        {
            huntedOrder.Clear();
            foreach (UnityEngine.Object loaded in FindAll("EnemyAI"))
            {
                EnemyAI enemy = loaded as EnemyAI;
                if (enemy == null || enemy.isEnemyDead || enemy.agent == null ||
                    enemy is DressGirlAI || !SharesArea(enemy, target))
                    continue;
                int instanceId = enemy.GetInstanceID();
                HuntedEnemy entry;
                if (!hunted.TryGetValue(instanceId, out entry))
                {
                    entry = new HuntedEnemy { enemy = enemy };
                    hunted[instanceId] = entry;
                }
                huntedOrder.Add(entry);
            }
            if (hunted.Count == huntedOrder.Count)
                return;
            hunted.Clear();
            foreach (HuntedEnemy entry in huntedOrder)
                hunted[entry.enemy.GetInstanceID()] = entry;
        }

        private void DriveHunted(HuntedEnemy entry, PlayerControllerB target,
                                 PlayerControllerB local, Vector3 goal, bool noise)
        {
            EnemyAI enemy = entry.enemy;
            if (enemy == null || enemy.isEnemyDead)
                return;
            float now = Time.unscaledTime;
            float distance = Vector3.Distance(enemy.transform.position, target.transform.position);
            bool mine = EnemyOwnedBy(enemy, local.actualClientId);
            bool selfTarget = target == local;
            if (entry.engaged)
            {
                bool settled = now - entry.engagedAt > 1f;
                bool lost = distance > RetakeDistance ||
                            (settled && !EnemyOwnedBy(enemy, target.actualClientId));
                if (!lost)
                {
                    if (lureRelentless && now >= entry.nextAggravation)
                    {
                        entry.nextAggravation = now + AggravateRetrySeconds;
                        AggravateEnemy(enemy, target, false);
                    }
                    return;
                }
                entry.engaged = false;
            }
            if (!mine && !selfTarget && distance <= RetakeDistance &&
                EnemyOwnedBy(enemy, target.actualClientId))
            {
                entry.engaged = true;
                entry.engagedAt = now;
                entry.nextAggravation = now;
                return;
            }
            if (!mine)
            {
                if (now >= entry.nextOwnershipRequest)
                {
                    entry.nextOwnershipRequest = now + OwnershipRetrySeconds;
                    enemy.ChangeEnemyOwnerServerRpc(local.actualClientId);
                }
                return;
            }
            if (!selfTarget && distance <= EngageDistance)
            {
                if (lureRelentless)
                    AggravateEnemy(enemy, target, true);
                enemy.ChangeEnemyOwnerServerRpc(target.actualClientId);
                entry.engaged = true;
                entry.engagedAt = now;
                entry.nextAggravation = now + AggravateRetrySeconds;
                return;
            }
            if (selfTarget && !lureRelentless && distance <= EngageDistance)
                return;
            if (selfTarget && lureRelentless)
            {
                enemy.targetPlayer = local;
                if (now >= entry.nextAggravation)
                {
                    entry.nextAggravation = now + AggravateRetrySeconds;
                    AggravateEnemy(enemy, local, true);
                }
            }
            if (lureRelentless && local.IsHost && enemy.openDoorSpeedMultiplier < HuntDoorSpeed)
                enemy.openDoorSpeedMultiplier = HuntDoorSpeed;
            SteerEnemy(enemy, goal, distance, lureRelentless);
            if (noise)
                enemy.DetectNoise(target.transform.position, 1f, 0, 0);
        }

        private void UpdateLure()
        {
            if (lureClientId < 0)
                return;
            PlayerControllerB local = LocalPlayer();
            PlayerControllerB target = PlayerByClientId(lureClientId);
            if (local == null || target == null || Time.unscaledTime >= lureEndsAt)
            {
                StopLure();
                return;
            }
            if (target.isPlayerDead || !target.isPlayerControlled)
            {
                if (!lureRelentless)
                    StopLure();
                return;
            }
            float now = Time.unscaledTime;
            if (now >= nextLureScan)
            {
                nextLureScan = now + 0.25f;
                RefreshHunted(target);
            }
            bool noise = now >= nextLureNoise;
            if (noise)
                nextLureNoise = now + 0.5f;
            Vector3 goal = SnapToNavMesh(target.transform.position);
            for (int i = 0; i < huntedOrder.Count; i++)
            {
                try
                {
                    DriveHunted(huntedOrder[i], target, local, goal, noise);
                }
                catch (Exception e)
                {
                    Debug.LogError("[Kue] Lure step failed: " + e);
                }
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
            reportedEnemyTraits = new int[0];
            nextEnemyTraitsRefresh = 0f;
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
            if (hazardCatalogReady &&
                Resources.FindObjectsOfTypeAll<IndoorMapHazardType>().Length * 65536 +
                        Resources.FindObjectsOfTypeAll<SpawnableOutsideObject>().Length !=
                    hazardCatalogSignature)
            {
                hazardCatalogReady = false;
                Debug.Log("[Kue] Traps changed; rescanning the catalog");
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

        private static string MoonName(SelectableLevel level, bool unlisted, bool hasScene)
        {
            string name = string.IsNullOrEmpty(level.PlanetName) ? level.name : level.PlanetName;
            if (unlisted)
                name += " (unlisted)";
            if (!hasScene)
                name += " (no map)";
            return name;
        }

        private static bool SceneInBuild(string sceneName)
        {
            if (string.IsNullOrEmpty(sceneName))
                return false;
            try
            {
                return SceneUtility.GetBuildIndexByScenePath(sceneName) >= 0 ||
                       SceneUtility.GetBuildIndexByScenePath("Assets/Scenes/" + sceneName +
                                                             ".unity") >= 0;
            }
            catch (Exception)
            {
                return false;
            }
        }

        private void RestoreSwappedLevel(StartOfRound round)
        {
            if (swappedLevelSlot >= 0 && round.levels != null &&
                swappedLevelSlot < round.levels.Length && swappedOriginalLevel != null)
                round.levels[swappedLevelSlot] = swappedOriginalLevel;
            swappedLevelSlot = -1;
            swappedOriginalLevel = null;
        }

        private SelectableLevel StandInMoon(SelectableLevel level)
        {
            SelectableLevel fallback = null;
            for (int i = 0; i < moonCatalog.Count; i++)
            {
                SelectableLevel candidate = moonCatalog[i];
                if (candidate == level || !moonHasScene[i] || moonLevelIndices[i] < 0)
                    continue;
                if (candidate.name == "ArtificeLevel")
                    return candidate;
                if (fallback == null)
                    fallback = candidate;
            }
            return fallback;
        }

        private void RefreshMoonCatalog()
        {
            if (moonCatalogReady)
                return;
            StartOfRound round = StartOfRound.Instance;
            if (round == null || round.levels == null)
                return;
            RestoreSwappedLevel(round);
            SelectableLevel[] loaded = Resources.FindObjectsOfTypeAll<SelectableLevel>();
            moonCatalogSignature = MoonCatalogSignature(round, loaded.Length);
            moonCatalogBuffer.Clear();
            moonLevelBuffer.Clear();
            moonSceneBuffer.Clear();
            catalogInstanceIds.Clear();
            for (int i = 0; i < round.levels.Length; i++)
            {
                SelectableLevel level = round.levels[i];
                if (level == null || !catalogInstanceIds.Add(level.GetInstanceID()))
                    continue;
                moonCatalogBuffer.Add(level);
                moonLevelBuffer.Add(i);
                moonSceneBuffer.Add(SceneInBuild(level.sceneName));
            }
            int routable = moonCatalogBuffer.Count;
            foreach (SelectableLevel level in loaded)
            {
                if (level == null || !catalogInstanceIds.Add(level.GetInstanceID()))
                    continue;
                moonCatalogBuffer.Add(level);
                moonLevelBuffer.Add(-1);
                moonSceneBuffer.Add(SceneInBuild(level.sceneName));
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
                string name = UniqueCatalogName(
                    MoonName(level, moonLevelBuffer[i] < 0, moonSceneBuffer[i]));
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
            moonHasScene.Clear();
            moonHasScene.AddRange(moonSceneBuffer);
            moonCatalogReady = true;
            Debug.Log("[Kue] Runtime moon catalog: " + moonCatalog.Count + " moons, " + routable +
                      " routable");
        }

        private void TravelToMoonManaged(int index)
        {
            StartOfRound round = StartOfRound.Instance;
            PlayerControllerB local = LocalPlayer();
            if (round == null || local == null || round.levels == null || index < 0 ||
                index >= moonCatalog.Count)
            {
                ReportActionFailure("Moon travel failed: catalog entry unavailable");
                return;
            }
            SelectableLevel level = moonCatalog[index];
            int levelIndex = moonLevelIndices[index];
            bool hasScene = moonHasScene[index];
            string name = MoonName(level, levelIndex < 0, hasScene);
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
            RestoreSwappedLevel(round);
            string detail = "";
            if (!hasScene)
            {
                SelectableLevel standIn = StandInMoon(level);
                if (standIn == null)
                {
                    ReportActionFailure("Moon travel failed: no moon with a map to borrow");
                    return;
                }
                level.sceneName = standIn.sceneName;
                detail = " on the map of " + MoonName(standIn, false, true);
            }
            if (levelIndex < 0)
            {
                if (!local.IsHost || round.connectedPlayersAmount > 0)
                {
                    ReportActionFailure("Moon travel failed: " + name +
                                        " needs you to be host with nobody else connected");
                    return;
                }
                int slot = Mathf.Clamp(round.currentLevelID, 0, round.levels.Length - 1);
                swappedOriginalLevel = round.levels[slot];
                round.levels[slot] = level;
                swappedLevelSlot = slot;
                levelIndex = slot;
            }
            round.ChangeLevelServerRpc(levelIndex, terminal.groupCredits);
            Debug.Log("[Kue] Routing the ship to " + name + " (level " + levelIndex + ")" + detail);
            if (HUDManager.Instance != null)
                HUDManager.Instance.DisplayTip("Solar", "Routing to " + name + detail);
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
            hazardCatalogReady = false;
            enemyCatalog.Clear();
            itemCatalog.Clear();
            moonCatalog.Clear();
            hazardCatalog.Clear();
            reportedEnemyTraits = new int[0];
            StopLure();
            moonLevelIndices.Clear();
            moonHasScene.Clear();
            swappedLevelSlot = -1;
            swappedOriginalLevel = null;
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

        private struct MapNodes
        {
            public int facility;
            public int dry;
            public int water;
            public int outside;
            public bool weeds;
        }

        private static int LiveNodeCount(GameObject[] nodes)
        {
            int live = 0;
            if (nodes != null)
                foreach (GameObject node in nodes)
                    if (node != null)
                        live++;
            return live;
        }

        private static List<GameObject> LiveNodes(GameObject[] nodes)
        {
            List<GameObject> live = new List<GameObject>();
            if (nodes != null)
                foreach (GameObject node in nodes)
                    if (node != null)
                        live.Add(node);
            return live;
        }

        private static GameObject[] HabitatNodes(EnemyType type)
        {
            RoundManager manager = RoundManager.Instance;
            if (!type.isOutsideEnemy)
                return manager.insideAINodes;
            manager.GetOutsideAINodes(true);
            switch (type.WaterType)
            {
            case EnemyWaterType.LandOnly:
                return manager.outsideAIDryNodesUnordered;
            case EnemyWaterType.WaterOnly:
                return manager.outsideAIWaterNodesUnordered;
            default:
                return manager.outsideAINodesUnordered;
            }
        }

        private static bool WeedsGrowing()
        {
            MoldSpreadManager mold = UnityEngine.Object.FindObjectOfType<MoldSpreadManager>();
            if (mold == null || mold.generatedMold == null)
                return false;
            foreach (GameObject weed in mold.generatedMold)
                if (weed != null && weed.activeSelf)
                    return true;
            return false;
        }

        private static MapNodes CountMapNodes()
        {
            MapNodes nodes = new MapNodes();
            RoundManager manager = RoundManager.Instance;
            StartOfRound round = StartOfRound.Instance;
            if (manager == null || round == null || round.inShipPhase)
                return nodes;
            manager.GetOutsideAINodes(true);
            nodes.facility = LiveNodeCount(manager.insideAINodes);
            nodes.dry = LiveNodeCount(manager.outsideAIDryNodesUnordered);
            nodes.water = LiveNodeCount(manager.outsideAIWaterNodesUnordered);
            nodes.outside = LiveNodeCount(manager.outsideAINodesUnordered);
            nodes.weeds = WeedsGrowing();
            return nodes;
        }

        private static bool EnemyWorksOnMap(EnemyType type, MapNodes nodes)
        {
            if (type.spawnFromWeeds && !nodes.weeds)
                return false;
            if (!type.isOutsideEnemy)
                return nodes.facility > 0;
            switch (type.WaterType)
            {
            case EnemyWaterType.LandOnly:
                return nodes.dry > 0;
            case EnemyWaterType.WaterOnly:
                return nodes.water > 0;
            default:
                return nodes.outside > 0;
            }
        }

        private void RefreshEnemyTraits()
        {
            if (!enemyCatalogReady || !menuOpen || Time.unscaledTime < nextEnemyTraitsRefresh)
                return;
            nextEnemyTraitsRefresh = Time.unscaledTime + 1f;
            if (reportedEnemyTraits.Length != enemyCatalog.Count)
            {
                reportedEnemyTraits = new int[enemyCatalog.Count];
                for (int i = 0; i < reportedEnemyTraits.Length; i++)
                    reportedEnemyTraits[i] = -1;
            }
            MapNodes nodes = CountMapNodes();
            for (int i = 0; i < enemyCatalog.Count; i++)
            {
                EnemyType type = enemyCatalog[i];
                if (type == null)
                    continue;
                int outside = type.isOutsideEnemy ? 1 : 0;
                int supported = EnemyWorksOnMap(type, nodes) ? 1 : 0;
                int traits = outside | (supported << 1);
                if (reportedEnemyTraits[i] == traits)
                    continue;
                EnemyTraitsResult result =
                    NativeBridge.ReportEnemySpawnTraits(i, outside, supported);
                if (result != EnemyTraitsResult.Recorded)
                {
                    Debug.LogError("[Kue] Enemy traits report failed at " + i + ": " + result);
                    return;
                }
                reportedEnemyTraits[i] = traits;
            }
        }

        private static Vector3 SnapToNavMesh(Vector3 position)
        {
            NavMeshHit hit;
            if (NavMesh.SamplePosition(position, out hit, 6f, NavMesh.AllAreas) ||
                NavMesh.SamplePosition(position, out hit, 40f, NavMesh.AllAreas))
                return hit.position;
            return position;
        }

        private static bool Reachable(Vector3 from, Vector3 to)
        {
            NavMeshPath path = new NavMeshPath();
            return NavMesh.CalculatePath(from, to, NavMesh.AllAreas, path) &&
                   path.status == NavMeshPathStatus.PathComplete;
        }

        private static Vector3 SpotNearPlayer(PlayerControllerB target, float distance)
        {
            Vector3 origin = SnapToNavMesh(TargetPosition(target));
            if (distance < 1f)
                return origin;
            float tolerance = Mathf.Max(3f, distance * 0.35f);
            for (int attempt = 0; attempt < 16; attempt++)
            {
                float angle = UnityEngine.Random.Range(0f, Mathf.PI * 2f);
                Vector3 candidate =
                    origin + new Vector3(Mathf.Cos(angle), 0f, Mathf.Sin(angle)) * distance;
                NavMeshHit hit;
                if (NavMesh.SamplePosition(candidate, out hit, tolerance, NavMesh.AllAreas) &&
                    Reachable(hit.position, origin))
                    return hit.position;
            }
            return origin;
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

        private bool SpawnEnemyAt(EnemyType type, Vector3 position)
        {
            SpawnNestForEnemy(type, position);
            NetworkObjectReference reference = RoundManager.Instance.SpawnEnemyGameObject(
                position, UnityEngine.Random.Range(0f, 360f), -1, type);
            NetworkObject networkObject;
            return reference.TryGet(out networkObject) && networkObject != null;
        }

        private EnemyType SpawnableEnemyType(int index, PlayerControllerB local)
        {
            if (index < 0 || index >= enemyCatalog.Count || RoundManager.Instance == null ||
                StartOfRound.Instance == null || local == null)
            {
                ReportActionFailure("Enemy spawn failed: session or type unavailable");
                return null;
            }
            if (!local.IsHost)
            {
                ReportActionFailure("Only the host can create enemies");
                return null;
            }
            EnemyType type = enemyCatalog[index];
            if (type == null || !EnemyWorksOnMap(type, CountMapNodes()))
            {
                ReportActionFailure((type == null ? "That enemy" : type.enemyName) +
                                    " cannot live on this map");
                return null;
            }
            return type;
        }

        private void SpawnEnemyManaged(int index, int count)
        {
            EnemyType type = SpawnableEnemyType(index, LocalPlayer());
            if (type == null)
                return;
            List<GameObject> nodes = LiveNodes(HabitatNodes(type));
            if (nodes.Count == 0)
            {
                ReportActionFailure(type.enemyName + " has nowhere to live on this map");
                return;
            }
            int spawned = 0;
            for (int i = 0; i < Mathf.Clamp(count, 1, MaximumSpawnCount); i++)
                if (SpawnEnemyAt(
                        type, nodes[UnityEngine.Random.Range(0, nodes.Count)].transform.position))
                    spawned++;
            string area = type.isOutsideEnemy ? " outside" : " in the facility";
            Debug.Log("[Kue] Spawned " + spawned + " x " + type.enemyName + area);
            ShowTip("Spawned " + spawned + " " + type.enemyName + area);
        }

        private void SpawnEnemyAtPlayerManaged(int index, int count, int clientId, int distance)
        {
            PlayerControllerB target = PlayerByClientId(clientId);
            if (target == null || target.isPlayerDead)
            {
                ReportActionFailure("Enemy spawn failed: that player is not alive");
                return;
            }
            EnemyType type = SpawnableEnemyType(index, LocalPlayer());
            if (type == null)
                return;
            if (type.isOutsideEnemy == target.isInsideFactory)
            {
                ReportActionFailure(type.enemyName + " only works " +
                                    (type.isOutsideEnemy ? "outside" : "inside the facility") +
                                    ", and " + target.playerUsername + " is not there");
                return;
            }
            float ring = Mathf.Max(MinimumEnemySpawnDistance, distance);
            int spawned = 0;
            for (int i = 0; i < Mathf.Clamp(count, 1, MaximumSpawnCount); i++)
                if (SpawnEnemyAt(type, SpotNearPlayer(target, ring)))
                    spawned++;
            Debug.Log("[Kue] Spawned " + spawned + " x " + type.enemyName + " about " + ring +
                      " m from " + target.playerUsername);
            ShowTip("Spawned " + spawned + " " + type.enemyName + " near " + target.playerUsername);
        }

        private void SummonEnemyTypeAtPlayerManaged(int index, int count, int clientId,
                                                    int distance)
        {
            PlayerControllerB target = PlayerByClientId(clientId);
            PlayerControllerB local = LocalPlayer();
            if (target == null || local == null || index < 0 || index >= enemyCatalog.Count)
            {
                ReportActionFailure("Summon failed: target or type unavailable");
                return;
            }
            EnemyType selected = enemyCatalog[index];
            List<EnemyAI> chosen = new List<EnemyAI>();
            foreach (UnityEngine.Object loaded in UnityEngine.Object.FindObjectsOfType<EnemyAI>())
            {
                EnemyAI enemy = loaded as EnemyAI;
                if (chosen.Count >= Mathf.Clamp(count, 1, MaximumSpawnCount))
                    break;
                if (enemy != null && !enemy.isEnemyDead && enemy.enemyType == selected &&
                    SharesArea(enemy, target))
                    chosen.Add(enemy);
            }
            if (chosen.Count == 0)
            {
                ReportActionFailure("No living " + selected.enemyName + " can reach " +
                                    target.playerUsername);
                return;
            }
            StartCoroutine(MoveEnemiesToPlayer(chosen, target, distance));
        }

        private void TeleportAllEnemiesManaged(PlayerControllerB target)
        {
            if (target == null || LocalPlayer() == null)
            {
                ReportActionFailure("Enemy teleport failed: target unavailable");
                return;
            }
            List<EnemyAI> chosen = new List<EnemyAI>();
            foreach (UnityEngine.Object loaded in UnityEngine.Object.FindObjectsOfType<EnemyAI>())
            {
                EnemyAI enemy = loaded as EnemyAI;
                if (enemy != null && !enemy.isEnemyDead && SharesArea(enemy, target))
                    chosen.Add(enemy);
            }
            if (chosen.Count == 0)
            {
                ReportActionFailure("No living enemy can reach " + target.playerUsername);
                return;
            }
            StartCoroutine(MoveEnemiesToPlayer(chosen, target, 0f));
        }

        private static void WarpEnemy(EnemyAI enemy, Vector3 position)
        {
            Vector3 previous = enemy.transform.position;
            NavMeshAgent agent = enemy.agent;
            if (agent == null || !agent.enabled || !agent.isOnNavMesh || !agent.Warp(position))
                enemy.transform.position = position;
            enemy.serverPosition = previous;
            enemy.SyncPositionToClients();
        }

        private IEnumerator MoveEnemiesToPlayer(List<EnemyAI> enemies, PlayerControllerB target,
                                                float distance)
        {
            PlayerControllerB local = LocalPlayer();
            if (local == null || target == null)
                yield break;
            float ring = Mathf.Max(MinimumEnemySpawnDistance, distance);
            Vector3[] spots = new Vector3[enemies.Count];
            bool[] moved = new bool[enemies.Count];
            for (int i = 0; i < spots.Length; i++)
                spots[i] = SpotNearPlayer(target, ring);
            int settledFrames = 0;
            for (int frame = 0; frame < 240 && settledFrames < 6; frame++)
            {
                bool allMoved = true;
                for (int i = 0; i < enemies.Count; i++)
                {
                    EnemyAI enemy = enemies[i];
                    if (enemy == null || enemy.isEnemyDead)
                        continue;
                    if (!EnemyOwnedBy(enemy, local.actualClientId))
                    {
                        allMoved = false;
                        if (frame % 20 == 0)
                            enemy.ChangeEnemyOwnerServerRpc(local.actualClientId);
                        continue;
                    }
                    if (!moved[i] || (enemy.transform.position - spots[i]).sqrMagnitude > 4f)
                        WarpEnemy(enemy, spots[i]);
                    moved[i] = true;
                }
                if (allMoved)
                    settledFrames++;
                yield return null;
            }
            int count = 0;
            for (int i = 0; i < enemies.Count; i++)
            {
                EnemyAI enemy = enemies[i];
                if (!moved[i] || enemy == null || enemy.isEnemyDead)
                    continue;
                count++;
                if (target != local && !target.isPlayerDead &&
                    EnemyOwnedBy(enemy, local.actualClientId))
                    enemy.ChangeEnemyOwnerServerRpc(target.actualClientId);
            }
            Debug.Log("[Kue] Teleported " + count + "/" + enemies.Count + " enemies to " +
                      target.playerUsername);
            ShowTip("Teleported " + count + " enemies to " + target.playerUsername);
        }

        private static Vector3 FloorBelow(Vector3 position, float verticalOffset)
        {
            StartOfRound round = StartOfRound.Instance;
            int mask =
                round != null ? round.collidersAndRoomMaskAndDefault : Physics.DefaultRaycastLayers;
            RaycastHit hit;
            if (Physics.Raycast(position, Vector3.down, out hit, 80f, mask,
                                QueryTriggerInteraction.Ignore))
                return hit.point + Vector3.up * (0.04f + verticalOffset);
            return position;
        }

        private static void RestItemOnFloor(GrabbableObject item, PlayerControllerB local,
                                            bool inElevator, bool inShipRoom)
        {
            StartOfRound round = StartOfRound.Instance;
            Transform parent = inElevator ? round.elevatorTransform : round.propsContainer;
            item.transform.SetParent(parent, true);
            Vector3 floor = FloorBelow(item.transform.position, item.itemProperties.verticalOffset);
            item.startFallingPosition = parent.InverseTransformPoint(item.transform.position);
            item.targetFloorPosition = parent.InverseTransformPoint(floor);
            item.floorYRot = -1;
            item.fallTime = 0f;
            item.hasHitGround = false;
            item.reachedFloorTarget = false;
            item.EnablePhysics(true);
            local.SetItemInElevator(inShipRoom, inElevator, item);
        }

        private IEnumerator SettleItems(List<GrabbableObject> items, List<int> scrapValues,
                                        bool inElevator, bool inShipRoom)
        {
            yield return null;
            yield return null;
            PlayerControllerB local = LocalPlayer();
            StartOfRound round = StartOfRound.Instance;
            if (local == null || round == null)
                yield break;
            foreach (GrabbableObject item in items)
                if (item != null)
                    RestItemOnFloor(item, local, inElevator, inShipRoom);
            if (round.connectedPlayersAmount <= 0)
                yield break;
            yield return new WaitForSeconds(0.3f);
            if (ThrowObjectServerRpcMethod == null)
                throw new MissingMethodException("PlayerControllerB.ThrowObjectServerRpc");
            List<NetworkObjectReference> scrapReferences = new List<NetworkObjectReference>();
            List<int> syncedValues = new List<int>();
            for (int i = 0; i < items.Count; i++)
            {
                GrabbableObject item = items[i];
                if (item == null || item.NetworkObject == null || !item.NetworkObject.IsSpawned ||
                    item.isHeld)
                    continue;
                NetworkObjectReference reference = item.NetworkObject;
                if (scrapValues != null && scrapValues[i] >= 0)
                {
                    scrapReferences.Add(reference);
                    syncedValues.Add(scrapValues[i]);
                }
                ThrowObjectServerRpcMethod.Invoke(local,
                                                  new object[] { reference, inElevator, inShipRoom,
                                                                 item.targetFloorPosition, -1 });
            }
            RoundManager manager = RoundManager.Instance;
            if (scrapReferences.Count == 0 || manager == null)
                yield break;
            float levelValue = manager.totalScrapValueInLevel;
            int collected = manager.scrapCollectedInLevel;
            int found = manager.valueOfFoundScrapItems;
            manager.SyncScrapValuesClientRpc(scrapReferences.ToArray(), syncedValues.ToArray());
            manager.totalScrapValueInLevel = levelValue;
            manager.scrapCollectedInLevel = collected;
            manager.valueOfFoundScrapItems = found;
        }

        private void SpawnItemAtPlayerManaged(int index, int count, int clientId, int fixedValue)
        {
            PlayerControllerB target = PlayerByClientId(clientId);
            PlayerControllerB local = LocalPlayer();
            StartOfRound round = StartOfRound.Instance;
            if (target == null || local == null || round == null)
            {
                ReportActionFailure("Item spawn failed: target player unavailable");
                return;
            }
            if (!local.IsHost)
            {
                ReportActionFailure("Only the host can create items");
                return;
            }
            if (index < 0 || index >= itemCatalog.Count)
            {
                ReportActionFailure("Item spawn failed: catalog entry " + index + " of " +
                                    itemCatalog.Count + " unavailable");
                return;
            }
            Item item = itemCatalog[index];
            bool inElevator = target.isInElevator;
            bool inShipRoom = target.isInHangarShipRoom;
            Transform parent = inElevator ? round.elevatorTransform : round.propsContainer;
            Vector3 origin =
                target.transform.position + Vector3.up * 1.1f + TargetForward(target) * 1.2f;
            float valueScale =
                RoundManager.Instance != null ? RoundManager.Instance.scrapValueMultiplier : 1f;
            List<GrabbableObject> spawned = new List<GrabbableObject>();
            List<int> scrapValues = new List<int>();
            for (int i = 0; i < Mathf.Clamp(count, 1, MaximumSpawnCount); i++)
            {
                Vector2 scatter = UnityEngine.Random.insideUnitCircle * 0.5f;
                GameObject gameObject =
                    Instantiate(item.spawnPrefab, origin + new Vector3(scatter.x, 0f, scatter.y),
                                Quaternion.identity, parent);
                GrabbableObject grabbable = gameObject.GetComponent<GrabbableObject>();
                NetworkObject networkObject = gameObject.GetComponent<NetworkObject>();
                if (grabbable == null || networkObject == null)
                {
                    Destroy(gameObject);
                    continue;
                }
                int value = -1;
                if (item.isScrap)
                {
                    value = fixedValue >= 0
                                ? fixedValue
                                : (int)(UnityEngine.Random.Range(item.minValue, item.maxValue) *
                                        valueScale);
                    grabbable.SetScrapValue(value);
                }
                grabbable.fallTime = 0f;
                networkObject.Spawn(false);
                spawned.Add(grabbable);
                scrapValues.Add(value);
            }
            if (spawned.Count == 0)
            {
                ReportActionFailure("Item spawn failed: " + item.itemName +
                                    " has no grabbable network prefab");
                return;
            }
            StartCoroutine(SettleItems(spawned, scrapValues, inElevator, inShipRoom));
            Debug.Log("[Kue] Spawned " + spawned.Count + " x " + item.itemName + " at " +
                      target.playerUsername + (fixedValue >= 0 ? " worth " + fixedValue : ""));
            ShowTip("Spawned " + spawned.Count + " " + item.itemName);
        }

        private void TeleportItemsToPlayerManaged(PlayerControllerB target)
        {
            PlayerControllerB local = LocalPlayer();
            if (!RealPlayer(target) || local == null)
            {
                ReportActionFailure("Item teleport failed: target player unavailable");
                return;
            }
            if (!local.IsHost)
            {
                ReportActionFailure("Only the host can move every item");
                return;
            }
            Vector3 center =
                target.transform.position + Vector3.up * 1.1f + TargetForward(target) * 1.5f;
            List<GrabbableObject> moved = new List<GrabbableObject>();
            foreach (GrabbableObject item in UnityEngine.Object
                         .FindObjectsOfType<GrabbableObject>())
            {
                if (item == null || item.itemProperties == null || item.isHeld || item.isPocketed ||
                    item.heldByPlayerOnServer || item.deactivated)
                    continue;
                TakeOwnership(item);
                int column = moved.Count % 6;
                int row = (moved.Count / 6) % 6;
                item.transform.position =
                    center + new Vector3((column - 2.5f) * 0.3f, 0f, (row - 2.5f) * 0.3f);
                moved.Add(item);
            }
            StartCoroutine(
                SettleItems(moved, null, target.isInElevator, target.isInHangarShipRoom));
            Debug.Log("[Kue] Teleported " + moved.Count + " loose items to " +
                      target.playerUsername);
            ShowTip("Moved " + moved.Count + " items to " + target.playerUsername);
        }

        private List<EnemyAI> LivingEnemies()
        {
            List<EnemyAI> living = new List<EnemyAI>();
            foreach (UnityEngine.Object loaded in UnityEngine.Object.FindObjectsOfType<EnemyAI>())
            {
                EnemyAI enemy = loaded as EnemyAI;
                if (enemy != null && !enemy.isEnemyDead)
                    living.Add(enemy);
            }
            return living;
        }

        private void KillAllEnemiesManaged()
        {
            PlayerControllerB local = LocalPlayer();
            if (local == null)
            {
                ReportActionFailure("Enemy kill failed: local player unavailable");
                return;
            }
            StartCoroutine(KillEnemiesOnceOwned(LivingEnemies(), local));
        }

        private IEnumerator KillEnemiesOnceOwned(List<EnemyAI> enemies, PlayerControllerB local)
        {
            bool[] handled = new bool[enemies.Count];
            int killed = 0;
            int removed = 0;
            int immune = 0;
            int pending = enemies.Count;
            for (int frame = 0; frame < 240 && pending > 0; frame++)
            {
                for (int i = 0; i < enemies.Count; i++)
                {
                    EnemyAI enemy = enemies[i];
                    if (handled[i])
                        continue;
                    if (enemy == null || enemy.isEnemyDead)
                    {
                        handled[i] = true;
                        pending--;
                        continue;
                    }
                    if (!EnemyOwnedBy(enemy, local.actualClientId))
                    {
                        if (frame % 20 == 0)
                            enemy.ChangeEnemyOwnerServerRpc(local.actualClientId);
                        continue;
                    }
                    handled[i] = true;
                    pending--;
                    EnemyType type = enemy.enemyType;
                    if (type != null && type.canDie)
                    {
                        enemy.KillEnemyOnOwnerClient(false);
                        killed++;
                    }
                    else if (local.IsHost && enemy.thisNetworkObject != null &&
                             enemy.thisNetworkObject.IsSpawned)
                    {
                        enemy.thisNetworkObject.Despawn(true);
                        removed++;
                    }
                    else if (type != null && type.canBeDestroyed)
                    {
                        enemy.KillEnemyOnOwnerClient(true);
                        removed++;
                    }
                    else
                        immune++;
                }
                yield return null;
            }
            string result = "Killed " + killed + " enemies, removed " + removed;
            if (immune + pending > 0)
                result += "; " + (immune + pending) + " cannot be killed from here";
            Debug.Log("[Kue] " + result);
            ShowTip(result);
        }

        private void StunAllEnemiesManaged()
        {
            PlayerControllerB local = LocalPlayer();
            if (local == null)
            {
                ReportActionFailure("Enemy stun failed: local player unavailable");
                return;
            }
            StartCoroutine(StunEnemiesOnceOwned(LivingEnemies(), local));
        }

        private IEnumerator StunEnemiesOnceOwned(List<EnemyAI> enemies, PlayerControllerB local)
        {
            bool[] handled = new bool[enemies.Count];
            int stunned = 0;
            int pending = 0;
            for (int i = 0; i < enemies.Count; i++)
            {
                handled[i] = enemies[i].enemyType == null || !enemies[i].enemyType.canBeStunned;
                if (!handled[i])
                    pending++;
            }
            int immune = enemies.Count - pending;
            for (int frame = 0; frame < 240 && pending > 0; frame++)
            {
                for (int i = 0; i < enemies.Count; i++)
                {
                    EnemyAI enemy = enemies[i];
                    if (handled[i])
                        continue;
                    if (enemy == null || enemy.isEnemyDead)
                    {
                        handled[i] = true;
                        pending--;
                        continue;
                    }
                    if (!EnemyOwnedBy(enemy, local.actualClientId))
                    {
                        if (frame % 20 == 0)
                            enemy.ChangeEnemyOwnerServerRpc(local.actualClientId);
                        continue;
                    }
                    handled[i] = true;
                    pending--;
                    enemy.SetEnemyStunned(true, StunSeconds, local);
                    stunned++;
                }
                yield return null;
            }
            string result = "Stunned " + stunned + " enemies for 60 s";
            if (immune > 0)
                result += "; " + immune + " cannot be stunned";
            Debug.Log("[Kue] " + result);
            ShowTip(result);
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
                TakeOwnership(item);
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
            if (moved == 0)
                ReportActionFailure("Deposit found no scrap in the ship");
            else
                ShowTip("Put " + moved + " scrap items on the counter");
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

        private void ToggleShipHorn()
        {
            ShipAlarmCord horn = FindOne("ShipAlarmCord") as ShipAlarmCord;
            if (horn == null)
            {
                shipHornEnabled = false;
                ReportActionFailure("Ship horn is not on this ship");
                return;
            }
            shipHornEnabled = !shipHornEnabled;
            if (shipHornEnabled)
                horn.HoldCordDown();
            else
                horn.StopHorn();
            Debug.Log("[Kue] Ship horn toggled " + shipHornEnabled);
        }

        private void ToggleCruiserHorn()
        {
            UnityEngine.Object[] cruisers = FindAll("VehicleController");
            if (cruisers.Length == 0)
            {
                carHornEnabled = false;
                ReportActionFailure("There is no cruiser");
                return;
            }
            carHornEnabled = !carHornEnabled;
            foreach (UnityEngine.Object cruiser in cruisers)
                InvokeAny(cruiser, "SetHonkServerRpc", carHornEnabled, -1);
        }

        private void ForceEjectEveryoneManaged()
        {
            StartOfRound round = StartOfRound.Instance;
            PlayerControllerB local = LocalPlayer();
            if (round == null || local == null || !local.IsHost || !round.inShipPhase)
            {
                ReportActionFailure("Firing the crew needs you to host with the ship in orbit");
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
                ReportActionFailure("Only the host can mark other players for the ghost girl");
                return;
            }
            float value = maximum ? player.maxInsanityLevel : 0f;
            player.insanityLevel = value;
            Debug.Log("[Kue] Set insanity for " + player.playerUsername + " to " + value);
            ShowTip(player.playerUsername + " insanity: " + Mathf.RoundToInt(value));
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

        private static bool PlushieTrigger(UnityEngine.Object trigger)
        {
            Component component = trigger as Component;
            return component != null && component.transform.parent != null &&
                   component.transform.parent.gameObject.name.StartsWith("PlushiePJManContainer");
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
                    if (PlushieTrigger(trigger))
                        InvokeAny(trigger, "TriggerAnimation", LocalPlayer());
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

        private static bool MaskCanBecomeMasked(UnityEngine.Object mask, bool host)
        {
            GrabbableObject item = mask as GrabbableObject;
            return item != null && !item.isHeld && !item.isPocketed &&
                   (host || GetMember(mask, "previousPlayerHeldBy") != null);
        }

        private void SpawnMaskedManaged()
        {
            PlayerControllerB local = LocalPlayer();
            bool host = local != null && local.IsHost;
            PlayerControllerB alive = null;
            StartOfRound round = StartOfRound.Instance;
            if (round != null && round.allPlayerScripts != null)
                foreach (PlayerControllerB player in round.allPlayerScripts)
                    if (RealPlayer(player) && !player.isPlayerDead)
                    {
                        alive = player;
                        break;
                    }
            int created = 0;
            foreach (UnityEngine.Object mask in FindAll("HauntedMaskItem"))
            {
                if (!MaskCanBecomeMasked(mask, host))
                    continue;
                Component component = mask as Component;
                if (host && GetMember(mask, "previousPlayerHeldBy") == null)
                    SetMember(mask, "previousPlayerHeldBy", alive);
                InvokeAny(mask, "CreateMimicServerRpc", BoolMember(mask, "isInFactory", false),
                          component.transform.position);
                created++;
            }
            if (created == 0)
                ReportActionFailure("No mask on the map can become a Masked right now");
        }

        private int HoardingBugRushIndex()
        {
            RoundManager manager = RoundManager.Instance;
            if (manager == null || manager.currentLevel == null ||
                manager.currentLevel.Enemies == null)
                return -1;
            List<SpawnableEnemyWithRarity> enemies = manager.currentLevel.Enemies;
            for (int i = 0; i < enemies.Count; i++)
                if (enemies[i] != null && enemies[i].enemyType != null &&
                    string.Equals(enemies[i].enemyType.enemyName, "Hoarding bug",
                                  StringComparison.OrdinalIgnoreCase))
                    return i;
            return -1;
        }

        private void StartHoardingBugInfestation()
        {
            int rushIndex = HoardingBugRushIndex();
            int catalogIndex;
            if (rushIndex < 0 || !enemyCatalogNames.TryGetValue("Hoarding bug", out catalogIndex))
            {
                ReportActionFailure("This moon has no Hoarding bug in its indoor spawn table");
                return;
            }
            SetMember(RoundManager.Instance, "enemyRushIndex", rushIndex);
            RoundManager.Instance.currentMaxInsidePower = 30f;
            SpawnEnemyManaged(catalogIndex, 1);
        }

        private void ToggleAllMines()
        {
            UnityEngine.Object[] mines = FindAll("Landmine");
            bool anyArmed = false;
            foreach (UnityEngine.Object mine in mines)
                anyArmed = anyArmed || BoolMember(mine, "mineActivated", false);
            foreach (UnityEngine.Object mine in mines)
            {
                Landmine landmine = mine as Landmine;
                if (landmine != null && !landmine.hasExploded)
                    landmine.ToggleMine(!anyArmed);
            }
        }

        private void ToggleAllTurrets()
        {
            UnityEngine.Object[] turrets = FindAll("Turret");
            bool anyActive = false;
            foreach (UnityEngine.Object loaded in turrets)
            {
                Turret turret = loaded as Turret;
                anyActive = anyActive || (turret != null && turret.turretActive);
            }
            foreach (UnityEngine.Object loaded in turrets)
            {
                Turret turret = loaded as Turret;
                if (turret != null)
                    turret.ToggleTurretEnabled(!anyActive);
            }
        }

        private void BerserkAllTurrets()
        {
            PlayerControllerB local = LocalPlayer();
            int playerId = local == null ? -1 : (int)local.playerClientId;
            foreach (UnityEngine.Object loaded in FindAll("Turret"))
            {
                Turret turret = loaded as Turret;
                if (turret != null)
                    turret.EnterBerserkModeServerRpc(playerId);
            }
        }

        private void ToggleFacilityPower()
        {
            BreakerBox breaker = FindOne("BreakerBox") as BreakerBox;
            PlayerControllerB local = LocalPlayer();
            if (breaker == null || RoundManager.Instance == null || local == null || !local.IsHost)
            {
                ReportActionFailure(
                    "Facility power needs you to host on a moon with a breaker box");
                return;
            }
            RoundManager.Instance.powerOffPermanently = false;
            RoundManager.Instance.SwitchPower(!breaker.isPowerOn);
        }

        private void SetHearEveryone(bool enabled)
        {
            hearEveryone = enabled;
            if (!enabled && StartOfRound.Instance != null)
                StartOfRound.Instance.UpdatePlayerVoiceEffects();
            Debug.Log("[Kue] Hear everyone " + enabled);
        }

        private void UpdateHearEveryone()
        {
            if (!hearEveryone)
                return;
            StartOfRound round = StartOfRound.Instance;
            PlayerControllerB local = LocalPlayer();
            if (round == null || local == null || round.allPlayerScripts == null)
                return;
            foreach (PlayerControllerB player in round.allPlayerScripts)
            {
                if (player == null || player == local ||
                    (!player.isPlayerControlled && !player.isPlayerDead))
                    continue;
                AudioSource voice = player.currentVoiceChatAudioSource;
                object voiceState = GetMember(player, "voicePlayerState");
                if (voice == null || voiceState == null)
                    continue;
                voice.spatialBlend = 0f;
                voice.panStereo = 0f;
                if (player.currentVoiceChatIngameSettings != null)
                    player.currentVoiceChatIngameSettings.set2D = true;
                SetMember(voiceState, "Volume", 1f);
                AudioLowPassFilter lowPass = voice.GetComponent<AudioLowPassFilter>();
                if (lowPass != null && lowPass.enabled)
                    lowPass.enabled = false;
                AudioHighPassFilter highPass = voice.GetComponent<AudioHighPassFilter>();
                if (highPass != null && highPass.enabled)
                    highPass.enabled = false;
            }
        }

        private static string HazardName(string assetName)
        {
            string name = assetName;
            foreach (string suffix in HazardNameSuffixes)
                if (name.Length > suffix.Length &&
                    name.EndsWith(suffix, StringComparison.OrdinalIgnoreCase))
                    name = name.Substring(0, name.Length - suffix.Length);
            System.Text.StringBuilder spaced = new System.Text.StringBuilder(name.Length + 8);
            for (int i = 0; i < name.Length; i++)
            {
                if (i > 0 && char.IsUpper(name[i]) && char.IsLower(name[i - 1]))
                    spaced.Append(' ');
                spaced.Append(name[i]);
            }
            return spaced.ToString().Trim();
        }

        private void AddHazardPrefab(GameObject prefab, string assetName)
        {
            if (prefab == null || string.IsNullOrEmpty(assetName) ||
                prefab.GetComponent<NetworkObject>() == null ||
                !catalogInstanceIds.Add(prefab.GetInstanceID()))
                return;
            hazardCatalogBuffer.Add(prefab);
            hazardNameBuffer.Add(HazardName(assetName));
        }

        private void RefreshHazardCatalog()
        {
            if (hazardCatalogReady)
                return;
            IndoorMapHazardType[] indoor = Resources.FindObjectsOfTypeAll<IndoorMapHazardType>();
            SpawnableOutsideObject[] outdoor =
                Resources.FindObjectsOfTypeAll<SpawnableOutsideObject>();
            hazardCatalogSignature = indoor.Length * 65536 + outdoor.Length;
            hazardCatalogBuffer.Clear();
            hazardNameBuffer.Clear();
            catalogInstanceIds.Clear();
            foreach (IndoorMapHazardType hazard in indoor)
                if (hazard != null)
                    AddHazardPrefab(hazard.prefabToSpawn, hazard.name);
            foreach (SpawnableOutsideObject hazard in outdoor)
                if (hazard != null)
                    AddHazardPrefab(hazard.prefabToSpawn, hazard.name);
            if (hazardCatalogBuffer.Count == 0)
                return;
            CatalogBeginResult begin = NativeBridge.BeginHazardCatalog();
            if (begin != CatalogBeginResult.Begun)
            {
                Debug.LogError("[Kue] Runtime trap catalog begin failed: " + begin);
                return;
            }
            usedCatalogNames.Clear();
            for (int i = 0; i < hazardCatalogBuffer.Count; i++)
            {
                CatalogReportResult report = NativeBridge.ReportHazardType(
                    hazardCatalogBuffer[i].GetInstanceID(), UniqueCatalogName(hazardNameBuffer[i]));
                if (report != CatalogReportResult.Recorded)
                {
                    NativeBridge.AbortHazardCatalog();
                    Debug.LogError("[Kue] Runtime trap catalog report failed at " + i + ": " +
                                   report);
                    return;
                }
            }
            CatalogCommitResult commit = NativeBridge.CommitHazardCatalog();
            if (commit != CatalogCommitResult.Committed && commit != CatalogCommitResult.Unchanged)
            {
                Debug.LogError("[Kue] Runtime trap catalog commit failed: " + commit);
                return;
            }
            hazardCatalog.Clear();
            hazardCatalog.AddRange(hazardCatalogBuffer);
            hazardCatalogReady = true;
            Debug.Log("[Kue] Runtime trap catalog: " + hazardCatalog.Count + " installed types");
        }

        private void SpawnHazardAtPlayerManaged(int index, int count, int clientId, int distance)
        {
            PlayerControllerB target = PlayerByClientId(clientId);
            PlayerControllerB local = LocalPlayer();
            RoundManager manager = RoundManager.Instance;
            if (target == null || local == null || index < 0 || index >= hazardCatalog.Count)
            {
                ReportActionFailure("Trap spawn failed: target or type unavailable");
                return;
            }
            if (!local.IsHost)
            {
                ReportActionFailure("Only the host can create traps");
                return;
            }
            if (manager == null || manager.mapPropsContainer == null)
            {
                ReportActionFailure("Trap spawn failed: land on a moon first");
                return;
            }
            GameObject prefab = hazardCatalog[index];
            int spawned = 0;
            for (int i = 0; i < Mathf.Clamp(count, 1, MaximumSpawnCount); i++)
            {
                GameObject hazard =
                    Instantiate(prefab, SpotNearPlayer(target, distance),
                                Quaternion.Euler(0f, UnityEngine.Random.Range(0f, 360f), 0f),
                                manager.mapPropsContainer.transform);
                NetworkObject networkObject = hazard.GetComponent<NetworkObject>();
                if (networkObject == null)
                {
                    Destroy(hazard);
                    continue;
                }
                networkObject.Spawn(true);
                spawned++;
            }
            Debug.Log("[Kue] Placed " + spawned + " x " + prefab.name + " near " +
                      target.playerUsername);
            ShowTip("Placed " + spawned + " traps near " + target.playerUsername);
        }

        private bool PreviewsSupported()
        {
            if (previewUnavailable)
                return false;
            if (previewTextureShader != null && previewDepthMaterial != null)
                return true;
            previewTextureShader = Shader.Find("UI/Default");
            Shader depthShader = Shader.Find("Hidden/Internal-Colored");
            if (previewTextureShader == null || depthShader == null)
            {
                previewUnavailable = true;
                Debug.Log("[Kue] Model previews unavailable: built-in shaders missing");
                return false;
            }
            previewDepthMaterial =
                new Material(depthShader) { hideFlags = HideFlags.HideAndDontSave };
            previewDepthMaterial.SetColor("_Color", PreviewUntexturedColor);
            previewDepthMaterial.SetInt("_SrcBlend", (int)BlendMode.One);
            previewDepthMaterial.SetInt("_DstBlend", (int)BlendMode.Zero);
            previewDepthMaterial.SetInt("_ZWrite", 1);
            previewDepthMaterial.SetInt("_ZTest", (int)CompareFunction.LessEqual);
            previewDepthMaterial.SetInt("_Cull", (int)CullMode.Off);
            previewCommands = new CommandBuffer { name = "Kue.ModelPreview" };
            return true;
        }

        private GameObject PreviewPrefab(int kind, int index)
        {
            if (index < 0)
                return null;
            switch (kind)
            {
            case PreviewKindEnemy:
                return index < enemyCatalog.Count && enemyCatalog[index] != null
                           ? enemyCatalog[index].enemyPrefab
                           : null;
            case PreviewKindItem:
                return index < itemCatalog.Count && itemCatalog[index] != null
                           ? itemCatalog[index].spawnPrefab
                           : null;
            case PreviewKindTrap:
                return index < hazardCatalog.Count ? hazardCatalog[index] : null;
            default:
                return null;
            }
        }

        private static bool ActiveWithin(Transform part, Transform root)
        {
            for (Transform current = part; current != null; current = current.parent)
            {
                if (!current.gameObject.activeSelf)
                    return false;
                if (current == root)
                    return true;
            }
            return true;
        }

        private static Bounds TransformedBounds(Bounds bounds, Matrix4x4 matrix)
        {
            Vector3 center = matrix.MultiplyPoint3x4(bounds.center);
            Vector3 extents = bounds.extents;
            Vector3 axisX = matrix.MultiplyVector(new Vector3(extents.x, 0f, 0f));
            Vector3 axisY = matrix.MultiplyVector(new Vector3(0f, extents.y, 0f));
            Vector3 axisZ = matrix.MultiplyVector(new Vector3(0f, 0f, extents.z));
            Vector3 size =
                new Vector3(Mathf.Abs(axisX.x) + Mathf.Abs(axisY.x) + Mathf.Abs(axisZ.x),
                            Mathf.Abs(axisX.y) + Mathf.Abs(axisY.y) + Mathf.Abs(axisZ.y),
                            Mathf.Abs(axisX.z) + Mathf.Abs(axisY.z) + Mathf.Abs(axisZ.z));
            return new Bounds(center, size * 2f);
        }

        private Material[] PreviewMaterials(Material[] sources, int submeshCount)
        {
            Material[] materials = new Material[submeshCount];
            for (int i = 0; i < submeshCount; i++)
            {
                Material source = sources != null && sources.Length > 0
                                      ? sources[Mathf.Min(i, sources.Length - 1)]
                                      : null;
                Texture texture = null;
                Color tint = Color.white;
                if (source != null)
                {
                    if (source.HasProperty("_BaseColorMap"))
                        texture = source.GetTexture("_BaseColorMap");
                    else if (source.HasProperty("_MainTex"))
                        texture = source.GetTexture("_MainTex");
                    if (source.HasProperty("_BaseColor"))
                        tint = source.GetColor("_BaseColor");
                    else if (source.HasProperty("_Color"))
                        tint = source.GetColor("_Color");
                }
                tint.a = 1f;
                Material material =
                    new Material(previewTextureShader) { hideFlags = HideFlags.HideAndDontSave };
                if (texture != null)
                    material.SetTexture("_MainTex", texture);
                material.SetColor("_Color", tint);
                materials[i] = material;
            }
            return materials;
        }

        private void ReleasePreviewModel()
        {
            foreach (PreviewPart part in previewParts)
            {
                if (part.ownsMesh && part.mesh != null)
                    Destroy(part.mesh);
                foreach (Material material in part.materials)
                    if (material != null)
                        Destroy(material);
            }
            previewParts.Clear();
            previewPrefab = null;
        }

        private void ReleasePreview()
        {
            ReleasePreviewModel();
            previewVisible = false;
            if (previewTexture != null)
            {
                previewTexture.Release();
                Destroy(previewTexture);
                previewTexture = null;
            }
            if (previewHolder != null)
            {
                Destroy(previewHolder);
                previewHolder = null;
            }
        }

        private void BuildPreviewModel(GameObject prefab)
        {
            ReleasePreviewModel();
            previewPrefab = prefab;
            if (previewHolder == null)
            {
                previewHolder = new GameObject("Kue.ModelPreview");
                previewHolder.hideFlags = HideFlags.HideAndDontSave;
                previewHolder.SetActive(false);
                DontDestroyOnLoad(previewHolder);
            }
            GameObject clone = Instantiate(prefab, previewHolder.transform);
            try
            {
                clone.transform.localPosition = Vector3.zero;
                clone.transform.localRotation = Quaternion.identity;
                int visibleLayers = PlayerVisibleLayers(GameCamera());
                bool hasBounds = false;
                foreach (Renderer renderer in HighestDetailRenderers(clone.transform))
                {
                    if (!ModelRenderer(renderer, visibleLayers) ||
                        !ActiveWithin(renderer.transform, clone.transform))
                        continue;
                    Mesh mesh = null;
                    bool ownsMesh = false;
                    SkinnedMeshRenderer skinned = renderer as SkinnedMeshRenderer;
                    if (skinned != null && skinned.sharedMesh != null)
                    {
                        mesh = new Mesh { hideFlags = HideFlags.HideAndDontSave };
                        skinned.BakeMesh(mesh, true);
                        ownsMesh = true;
                        if (mesh.vertexCount == 0)
                        {
                            Destroy(mesh);
                            mesh = skinned.sharedMesh;
                            ownsMesh = false;
                        }
                    }
                    else if (renderer is MeshRenderer)
                    {
                        MeshFilter filter = renderer.GetComponent<MeshFilter>();
                        mesh = filter != null ? filter.sharedMesh : null;
                    }
                    if (mesh == null || mesh.vertexCount == 0)
                        continue;
                    PreviewPart part =
                        new PreviewPart { mesh = mesh, ownsMesh = ownsMesh,
                                          matrix = renderer.transform.localToWorldMatrix,
                                          materials = PreviewMaterials(renderer.sharedMaterials,
                                                                       mesh.subMeshCount) };
                    Bounds bounds = TransformedBounds(mesh.bounds, part.matrix);
                    if (hasBounds)
                        previewBounds.Encapsulate(bounds);
                    else
                        previewBounds = bounds;
                    hasBounds = true;
                    previewParts.Add(part);
                }
            }
            finally
            {
                DestroyImmediate(clone);
            }
        }

        private void RenderPreview(int width, int height)
        {
            if (previewTexture == null || previewTexture.width != width ||
                previewTexture.height != height)
            {
                if (previewTexture != null)
                {
                    previewTexture.Release();
                    Destroy(previewTexture);
                }
                previewTexture = new RenderTexture(width, height, 24, RenderTextureFormat.ARGB32) {
                    hideFlags = HideFlags.HideAndDontSave, antiAliasing = 4
                };
                previewTexture.Create();
            }
            Vector3 center = previewBounds.center;
            float radius = Mathf.Max(previewBounds.extents.magnitude, 0.05f);
            float aspect = (float)width / height;
            float halfAngle = PreviewFieldOfView * 0.5f * Mathf.Deg2Rad;
            float distance = radius / Mathf.Sin(halfAngle) / Mathf.Min(1f, aspect) * 1.05f;
            Vector3 eye = center + new Vector3(0f, 0.22f, 1f).normalized *distance;
            Matrix4x4 view =
                Matrix4x4.Scale(new Vector3(1f, 1f, -1f)) *
                Matrix4x4.TRS(eye, Quaternion.LookRotation(center - eye, Vector3.up), Vector3.one)
                    .inverse;
            Matrix4x4 projection = Matrix4x4.Perspective(PreviewFieldOfView, aspect,
                                                         Mathf.Max(0.01f, distance - radius * 1.5f),
                                                         distance + radius * 1.5f);
            Matrix4x4 spin =
                Matrix4x4.Translate(center) *
                Matrix4x4.Rotate(Quaternion.Euler(0f, Time.unscaledTime * PreviewSpinDegrees, 0f)) *
                Matrix4x4.Translate(-center);
            Color background = QualitySettings.activeColorSpace == ColorSpace.Linear
                                   ? PreviewBackground.linear
                                   : PreviewBackground;
            float guiDepthTest = Shader.GetGlobalFloat(GuiDepthTestId);
            CommandBuffer commands = previewCommands;
            commands.Clear();
            commands.SetRenderTarget(previewTexture);
            commands.ClearRenderTarget(true, true, background);
            commands.SetViewProjectionMatrices(view, projection);
            foreach (PreviewPart part in previewParts)
                for (int submesh = 0; submesh < part.materials.Length; submesh++)
                    commands.DrawMesh(part.mesh, spin * part.matrix, previewDepthMaterial, submesh,
                                      0);
            commands.SetGlobalFloat(GuiDepthTestId, (float)CompareFunction.LessEqual);
            foreach (PreviewPart part in previewParts)
                for (int submesh = 0; submesh < part.materials.Length; submesh++)
                    commands.DrawMesh(part.mesh, spin * part.matrix, part.materials[submesh],
                                      submesh, 0);
            commands.SetGlobalFloat(GuiDepthTestId, guiDepthTest);
            RenderTexture active = RenderTexture.active;
            Graphics.ExecuteCommandBuffer(commands);
            RenderTexture.active = active;
        }

        private void UpdatePreview()
        {
            previewVisible = false;
            if (!menuOpen || !PreviewsSupported())
                return;
            int kind;
            int index;
            int x;
            int y;
            int width;
            int height;
            if (NativeBridge.GetPreviewRequest(out kind, out index, out x, out y, out width,
                                               out height) == 0 ||
                width <= 0 || height <= 0)
                return;
            try
            {
                GameObject prefab = PreviewPrefab(kind, index);
                if (prefab == null)
                    return;
                if (prefab != previewPrefab)
                    BuildPreviewModel(prefab);
                if (previewParts.Count == 0)
                    return;
                RenderPreview(width, height);
                previewRect = new Rect(x, y, width, height);
                previewVisible = true;
            }
            catch (Exception e)
            {
                previewUnavailable = true;
                ReleasePreview();
                Debug.LogError("[Kue] Model previews disabled: " + e);
            }
        }

        private static int Bit(SessionSwitch flag)
        {
            return 1 << (int)flag;
        }

        private static int Bit(WorldFeature flag)
        {
            return 1 << (int)flag;
        }

        private void ScanWorld()
        {
            int world = 0;
            int switches = 0;
            long inFacility = 0;
            StartOfRound round = StartOfRound.Instance;
            RoundManager manager = RoundManager.Instance;
            PlayerControllerB local = LocalPlayer();
            bool host = local != null && local.IsHost;
            bool orbit = round != null && round.inShipPhase && !round.travellingToNewLevel;
            bool moonLoaded = round != null && manager != null && !round.inShipPhase &&
                              manager.mapPropsContainer != null;
            if (host)
                world |= Bit(WorldFeature.Host);
            if (orbit)
                world |= Bit(WorldFeature.ShipInOrbit);
            if (moonLoaded)
                world |= Bit(WorldFeature.MoonLoaded);
            if (round != null && round.allowLocalPlayerDeath && round.shipDoorsEnabled &&
                round.timeSinceRoundStarted >= MinimumLocalDeathRoundTime)
                world |= Bit(WorldFeature.PlayersCanDie);
            if (livingEnemyCount > 0)
                world |= Bit(WorldFeature.EnemiesAlive);
            if (round != null && round.allPlayerScripts != null)
                foreach (PlayerControllerB player in round.allPlayerScripts)
                    if (RealPlayer(player) && player.isInsideFactory && player.playerClientId < 64)
                        inFacility |= 1L << (int)player.playerClientId;

            if (FindOne("ShipAlarmCord") != null)
                world |= Bit(WorldFeature.ShipHorn);
            ShipLights lights = FindOne("ShipLights") as ShipLights;
            if (lights != null)
            {
                world |= Bit(WorldFeature.ShipLights);
                if (lights.areLightsOn)
                    switches |= Bit(SessionSwitch.ShipLights);
            }
            if (FindOne("Terminal") != null)
                world |= Bit(WorldFeature.Terminal);
            if (FindOne("HangarShipDoor") != null)
                world |= Bit(WorldFeature.ShipDoor);
            foreach (UnityEngine.Object trigger in FindAll("AnimatedObjectTrigger"))
                if (PlushieTrigger(trigger))
                {
                    world |= Bit(WorldFeature.Plushie);
                    break;
                }
            if (FindAll("VehicleController").Length > 0)
            {
                world |= Bit(WorldFeature.Cruiser);
                if (round != null && round.magnetOn)
                    switches |= Bit(SessionSwitch.CruiserMagnet);
            }
            if (FindOne("DepositItemsDesk") != null)
                world |= Bit(WorldFeature.CompanyDesk);
            foreach (UnityEngine.Object mine in FindAll("Landmine"))
            {
                Landmine landmine = mine as Landmine;
                if (landmine == null || landmine.hasExploded)
                    continue;
                world |= Bit(WorldFeature.Landmines);
                if (BoolMember(mine, "mineActivated", false))
                    switches |= Bit(SessionSwitch.LandminesArmed);
            }
            foreach (UnityEngine.Object loaded in FindAll("Turret"))
            {
                Turret turret = loaded as Turret;
                if (turret == null)
                    continue;
                world |= Bit(WorldFeature.Turrets);
                if (turret.turretActive)
                    switches |= Bit(SessionSwitch.TurretsActive);
            }
            BreakerBox breaker = FindOne("BreakerBox") as BreakerBox;
            if (moonLoaded && breaker != null)
            {
                world |= Bit(WorldFeature.FacilityPower);
                if (breaker.isPowerOn)
                    switches |= Bit(SessionSwitch.FacilityPower);
            }
            UnityEngine.Object mainBridge = FindOne("BridgeTrigger");
            if (mainBridge != null && !BoolMember(mainBridge, "hasBridgeFallen", false))
                world |= Bit(WorldFeature.MainBridge);
            UnityEngine.Object smallBridge = FindOne("BridgeTriggerType2");
            if (smallBridge != null && !BoolMember(smallBridge, "bridgeFell", false))
                world |= Bit(WorldFeature.SmallBridge);
            if (FindOne("MineshaftElevatorController") != null)
                world |= Bit(WorldFeature.MineshaftElevator);
            if (FindAll("ShotgunItem").Length > 0)
                world |= Bit(WorldFeature.Shotguns);
            if (FindAll("JetpackItem").Length > 0)
                world |= Bit(WorldFeature.Jetpacks);
            foreach (UnityEngine.Object mask in FindAll("HauntedMaskItem"))
                if (MaskCanBecomeMasked(mask, host))
                {
                    world |= Bit(WorldFeature.HauntedMasks);
                    break;
                }
            if (moonLoaded && manager.insideAINodes != null && manager.insideAINodes.Length > 0 &&
                HoardingBugRushIndex() >= 0)
                world |= Bit(WorldFeature.HoardingBugNest);
            if (PreviewsSupported())
                world |= Bit(WorldFeature.ModelPreviews);
            scannedWorld = world;
            scannedSwitches = switches;
            scannedPlayersInFacility = inFacility;
        }

        private void ReportSession()
        {
            if (!menuOpen)
                return;
            if (Time.unscaledTime >= nextWorldScan)
            {
                nextWorldScan = Time.unscaledTime + 0.5f;
                ScanWorld();
            }
            int switches = scannedSwitches;
            if (flyEnabled)
                switches |= Bit(SessionSwitch.Fly);
            if (thirdPersonEnabled)
                switches |= Bit(SessionSwitch.ThirdPerson);
            if (slideTaunt)
                switches |= Bit(SessionSwitch.EmoteWhileMoving);
            if (hearEveryone)
                switches |= Bit(SessionSwitch.HearEveryone);
            if (shipHornEnabled)
                switches |= Bit(SessionSwitch.ShipHorn);
            if (terminalSpamEnabled)
                switches |= Bit(SessionSwitch.TerminalSound);
            if (openShipDoorSpace)
                switches |= Bit(SessionSwitch.ShipDoorButtons);
            if (pjManSpam)
                switches |= Bit(SessionSwitch.PlushieSpam);
            if (carHornEnabled)
                switches |= Bit(SessionSwitch.CruiserHorn);
            if (depositSpamEnabled)
                switches |= Bit(SessionSwitch.DeskDoorSound);
            if (shotgunSpamEnabled)
                switches |= Bit(SessionSwitch.ShotgunSpam);
            if (explodeJetpacksOnGrab)
                switches |= Bit(SessionSwitch.JetpackTrap);
            int secondsLeft = -1;
            if (lureClientId >= 0 && !lureRelentless)
                secondsLeft = Mathf.Max(0, Mathf.CeilToInt(lureEndsAt - Time.unscaledTime));
            NativeBridge.ReportSessionState(switches, scannedWorld, scannedPlayersInFacility,
                                            lureClientId, secondsLeft);
        }

        private void RunAction(PlayerAction action, int clientId, int catalogIndex, int count,
                               int amount)
        {
            switch (action)
            {
            case PlayerAction.TeleportTo:
                TeleportToPlayerManaged(PlayerByClientId(clientId));
                break;
            case PlayerAction.Kill:
                KillPlayerManaged(PlayerByClientId(clientId));
                break;
            case PlayerAction.HealSelf:
                HealSelfManaged();
                break;
            case PlayerAction.KillAll:
            case PlayerAction.KillAllExceptLocal:
            {
                PlayerControllerB local = LocalPlayer();
                foreach (PlayerControllerB player in StartOfRound.Instance.allPlayerScripts)
                    if (RealPlayer(player) && player != local)
                        KillPlayerManaged(player);
                if (action == PlayerAction.KillAll)
                    QueueLocalKill(local);
                break;
            }
            case PlayerAction.LureAllEnemies:
                ToggleLure(clientId, false);
                break;
            case PlayerAction.TogglePersistentLure:
                ToggleLure(clientId, true);
                break;
            case PlayerAction.TeleportAllEnemies:
                TeleportAllEnemiesManaged(PlayerByClientId(clientId));
                break;
            case PlayerAction.SpawnEnemy:
                SpawnEnemyManaged(catalogIndex, count);
                break;
            case PlayerAction.SpawnEnemyAtPlayer:
                SpawnEnemyAtPlayerManaged(catalogIndex, count, clientId, amount);
                break;
            case PlayerAction.SummonEnemyTypeAtPlayer:
                SummonEnemyTypeAtPlayerManaged(catalogIndex, count, clientId, amount);
                break;
            case PlayerAction.SpawnHazardAtPlayer:
                SpawnHazardAtPlayerManaged(catalogIndex, count, clientId, amount);
                break;
            case PlayerAction.KillAllEnemies:
                KillAllEnemiesManaged();
                break;
            case PlayerAction.StunAllEnemies:
                StunAllEnemiesManaged();
                break;
            case PlayerAction.SpawnItemAtPlayer:
                SpawnItemAtPlayerManaged(catalogIndex, count, clientId, amount);
                break;
            case PlayerAction.TeleportItemsToPlayer:
                TeleportItemsToPlayerManaged(PlayerByClientId(clientId));
                break;
            case PlayerAction.DepositShipScrap:
                DepositShipScrapManaged();
                break;
            case PlayerAction.AddTerminalCredits:
                AddTerminalCreditsManaged(amount);
                break;
            case PlayerAction.TravelToMoon:
                TravelToMoonManaged(catalogIndex);
                break;
            case PlayerAction.ClearInsanity:
                SetPlayerInsanityManaged(PlayerByClientId(clientId), false);
                break;
            case PlayerAction.MaxInsanity:
                SetPlayerInsanityManaged(PlayerByClientId(clientId), true);
                break;
            case PlayerAction.ToggleFly:
                SetFly(!flyEnabled);
                break;
            case PlayerAction.ToggleThirdPerson:
                SetThirdPerson(!thirdPersonEnabled);
                break;
            case PlayerAction.ToggleHearEveryone:
                SetHearEveryone(!hearEveryone);
                break;
            case PlayerAction.SlideTaunt:
                slideTaunt = !slideTaunt;
                break;
            case PlayerAction.ToggleShipHorn:
                ToggleShipHorn();
                break;
            case PlayerAction.ToggleCarHorn:
                ToggleCruiserHorn();
                break;
            case PlayerAction.ToggleShipLights:
                InvokeAny(FindOne("ShipLights"), "ToggleShipLights");
                break;
            case PlayerAction.ToggleTerminalSound:
                terminalSpamEnabled = !terminalSpamEnabled;
                break;
            case PlayerAction.ToggleDepositDeskSound:
                depositSpamEnabled = !depositSpamEnabled;
                break;
            case PlayerAction.OpenShipDoorSpace:
                openShipDoorSpace = !openShipDoorSpace;
                break;
            case PlayerAction.ToggleVehicleMagnet:
                StartOfRound.Instance.SetMagnetOnServerRpc(!StartOfRound.Instance.magnetOn);
                break;
            case PlayerAction.TogglePjManSpam:
                pjManSpamInterval = Mathf.Clamp(amount / 1000f, 0f, 1f);
                pjManSpam = !pjManSpam;
                nextPjManPulse = 0f;
                break;
            case PlayerAction.EjectEveryone:
                ForceEjectEveryoneManaged();
                break;
            case PlayerAction.ExplodeCruiser:
                foreach (UnityEngine.Object cruiser in FindAll("VehicleController"))
                    InvokeAny(cruiser, "DestroyCarServerRpc", -1);
                break;
            case PlayerAction.ForceTentacleAttack:
                ForceTentacleAttackManaged();
                break;
            case PlayerAction.ToggleAllMines:
                ToggleAllMines();
                break;
            case PlayerAction.BlowUpAllMines:
                foreach (UnityEngine.Object mine in FindAll("Landmine"))
                    InvokeAny(mine, "ExplodeMineServerRpc");
                break;
            case PlayerAction.ToggleAllTurrets:
                ToggleAllTurrets();
                break;
            case PlayerAction.BerserkAllTurrets:
                BerserkAllTurrets();
                break;
            case PlayerAction.ToggleFactoryLights:
                ToggleFacilityPower();
                break;
            case PlayerAction.ForceBridgeFall:
            {
                UnityEngine.Object bridge = FindOne("BridgeTrigger");
                if (TakeOwnership(bridge))
                    InvokeAny(bridge, "BridgeFallServerRpc");
                break;
            }
            case PlayerAction.ForceSmallBridgeFall:
            {
                UnityEngine.Object bridge = FindOne("BridgeTriggerType2");
                for (int i = 0; i < 4; i++)
                    InvokeAny(bridge, "AddToBridgeInstabilityServerRpc");
                break;
            }
            case PlayerAction.ToggleMineshaftElevator:
                InvokeAny(FindOne("MineshaftElevatorController"), "PressElevatorButtonServerRpc");
                break;
            case PlayerAction.SpawnHoardingBugInfestation:
                StartHoardingBugInfestation();
                break;
            case PlayerAction.ShootAllShotguns:
                ShootAllShotgunsManaged();
                break;
            case PlayerAction.ToggleShotgunSpam:
                shotgunSpamEnabled = !shotgunSpamEnabled;
                break;
            case PlayerAction.ExplodeAllJetpacks:
                ExplodeAllJetpacksManaged(false);
                break;
            case PlayerAction.ToggleExplodeJetpacksOnGrab:
                explodeJetpacksOnGrab = !explodeJetpacksOnGrab;
                break;
            case PlayerAction.SpawnMaskedEnemy:
                SpawnMaskedManaged();
                break;
            case PlayerAction.None:
                break;
            }
        }

        private void ProcessManagedActions()
        {
            int clientId;
            int catalogIndex;
            int count;
            int amount;
            PlayerAction action;
            while ((action = NativeBridge.PollAction(out clientId, out catalogIndex, out count,
                                                     out amount)) != PlayerAction.None)
            {
                StartOfRound round = StartOfRound.Instance;
                if (round == null || round.allPlayerScripts == null)
                    continue;
                try
                {
                    RunAction(action, clientId, catalogIndex, count, amount);
                }
                catch (Exception e)
                {
                    ReportActionFailure(action + " failed: " + e.Message);
                    Debug.LogError("[Kue] " + action + " failed: " + e);
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
                        HUDManager.Instance.DisplayTip("Solar", name + " has died");
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
                new RendererCacheEntry { owner = root, renderers = HighestDetailRenderers(root) };
            rendererCache[instanceId] = entry;
            return entry.renderers;
        }

        private static Renderer[] HighestDetailRenderers(Component root)
        {
            Renderer[] all = root.GetComponentsInChildren<Renderer>(true);
            LODGroup[] groups = root.GetComponentsInChildren<LODGroup>(true);
            if (groups.Length == 0)
                return all;
            HashSet<Renderer> detailed = new HashSet<Renderer>();
            HashSet<Renderer> reduced = new HashSet<Renderer>();
            foreach (LODGroup group in groups)
            {
                LOD[] levels = group.GetLODs();
                for (int level = 0; level < levels.Length; level++)
                    foreach (Renderer renderer in levels[level].renderers)
                        if (renderer != null)
                            (level == 0 ? detailed : reduced).Add(renderer);
            }
            List<Renderer> kept = new List<Renderer>(all.Length);
            foreach (Renderer renderer in all)
                if (detailed.Contains(renderer) || !reduced.Contains(renderer))
                    kept.Add(renderer);
            return kept.ToArray();
        }

        private static int PlayerVisibleLayers(Camera camera)
        {
            int layers = camera != null ? camera.cullingMask : ~0;
            foreach (string hidden in HelperLayerNames)
            {
                int layer = LayerMask.NameToLayer(hidden);
                if (layer >= 0)
                    layers &= ~(1 << layer);
            }
            int unseenEnemies = LayerMask.NameToLayer("EnemiesNotRendered");
            if (unseenEnemies >= 0)
                layers |= 1 << unseenEnemies;
            return layers;
        }

        private static bool ModelRenderer(Renderer renderer, int visibleLayers)
        {
            return renderer != null && renderer.enabled && !renderer.forceRenderingOff &&
                   renderer.sharedMaterial != null &&
                   renderer.shadowCastingMode != ShadowCastingMode.ShadowsOnly &&
                   (visibleLayers & (1 << renderer.gameObject.layer)) != 0 &&
                   (renderer is MeshRenderer || renderer is SkinnedMeshRenderer);
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
            livingEnemyCount = 0;
            foreach (UnityEngine.Object loaded in FindAll("EnemyAI"))
            {
                EnemyAI enemy = loaded as EnemyAI;
                if (enemy != null && !enemy.isEnemyDead)
                {
                    livingEnemyCount++;
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
            if (menuOpen && previewVisible && previewTexture != null)
                GUI.DrawTexture(previewRect, previewTexture, ScaleMode.StretchToFill, false);
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
            int visibleLayers = PlayerVisibleLayers(camera);
            foreach (Renderer renderer in mark.renderers)
            {
                if (!ModelRenderer(renderer, visibleLayers))
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
