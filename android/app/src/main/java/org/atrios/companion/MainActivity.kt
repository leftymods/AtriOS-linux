package org.atrios.companion

import android.Manifest
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import androidx.lifecycle.viewmodel.compose.viewModel
import org.atrios.companion.data.bluetooth.BtConnectionState
import org.atrios.companion.ui.screens.*
import org.atrios.companion.ui.theme.AtriOSCompanionTheme
import org.atrios.companion.viewmodel.StationViewModel

enum class NavigationTab(val title: String, val icon: androidx.compose.ui.graphics.vector.ImageVector) {
    DASHBOARD("Dashboard", Icons.Default.Dashboard),
    AUDIO("Audio", Icons.Default.VolumeUp),
    DISPLAY("Display", Icons.Default.Highlight),
    CONNECTIVITY("Wireless", Icons.Default.Wifi),
    SYSTEM("System", Icons.Default.Settings)
}

class MainActivity : ComponentActivity() {

    private val requestPermissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { permissions ->
        // Handle permissions
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()

        // Check & request Bluetooth permissions on Android 12+
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            val needed = arrayOf(
                Manifest.permission.BLUETOOTH_CONNECT,
                Manifest.permission.BLUETOOTH_SCAN
            ).filter {
                ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
            }
            if (needed.isNotEmpty()) {
                requestPermissionLauncher.launch(needed.toTypedArray())
            }
        }

        setContent {
            AtriOSCompanionTheme {
                MainAppScreen()
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MainAppScreen(viewModel: StationViewModel = viewModel()) {
    val uiState by viewModel.uiState.collectAsState()
    var currentTab by remember { mutableStateOf(NavigationTab.DASHBOARD) }
    val snackbarHostState = remember { SnackbarHostState() }

    LaunchedEffect(uiState.errorMessage) {
        uiState.errorMessage?.let { msg ->
            snackbarHostState.showSnackbar(msg)
            viewModel.clearMessages()
        }
    }

    LaunchedEffect(uiState.actionSuccessMessage) {
        uiState.actionSuccessMessage?.let { msg ->
            snackbarHostState.showSnackbar(msg)
            viewModel.clearMessages()
        }
    }

    Scaffold(
        modifier = Modifier.fillMaxSize(),
        snackbarHost = { SnackbarHost(hostState = snackbarHostState) },
        topBar = {
            TopAppBar(
                title = {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        modifier = Modifier.clickable {
                            if (!uiState.isConnected) viewModel.autoConnect()
                        }
                    ) {
                        Icon(
                            imageVector = when (uiState.btState) {
                                is BtConnectionState.Connected -> Icons.Default.BluetoothConnected
                                is BtConnectionState.Connecting -> Icons.Default.BluetoothSearching
                                else -> Icons.Default.Bluetooth
                            },
                            contentDescription = null,
                            tint = if (uiState.isConnected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant,
                            modifier = Modifier.size(24.dp)
                        )
                        Spacer(modifier = Modifier.width(10.dp))
                        Column {
                            Text("AtriOS Station", fontWeight = FontWeight.Bold, style = MaterialTheme.typography.titleMedium)
                            Text(
                                text = when (uiState.btState) {
                                    is BtConnectionState.Connected -> "Bluetooth: ${uiState.deviceName}"
                                    is BtConnectionState.Connecting -> "Connecting to ${uiState.btState.deviceName}..."
                                    else -> "Bluetooth: Disconnected (Tap to connect)"
                                },
                                style = MaterialTheme.typography.bodySmall,
                                color = if (uiState.isConnected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                    }
                },
                actions = {
                    IconButton(onClick = {
                        if (uiState.isConnected) {
                            viewModel.requestStatus()
                        } else {
                            viewModel.autoConnect()
                        }
                    }) {
                        Icon(
                            imageVector = if (uiState.isConnected) Icons.Default.Refresh else Icons.Default.BluetoothSearching,
                            contentDescription = "Sync"
                        )
                    }
                },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = MaterialTheme.colorScheme.surfaceContainer
                )
            )
        },
        bottomBar = {
            NavigationBar(
                containerColor = MaterialTheme.colorScheme.surfaceContainer
            ) {
                NavigationTab.entries.forEach { tab ->
                    NavigationBarItem(
                        selected = (currentTab == tab),
                        onClick = { currentTab = tab },
                        icon = { Icon(tab.icon, contentDescription = tab.title) },
                        label = { Text(tab.title) }
                    )
                }
            }
        }
    ) { innerPadding ->
        Box(modifier = Modifier.padding(innerPadding)) {
            when (currentTab) {
                NavigationTab.DASHBOARD -> DashboardScreen(uiState, viewModel)
                NavigationTab.AUDIO -> AudioScreen(uiState, viewModel)
                NavigationTab.DISPLAY -> DisplayScreen(uiState, viewModel)
                NavigationTab.CONNECTIVITY -> ConnectivityScreen(uiState, viewModel)
                NavigationTab.SYSTEM -> SystemScreen(uiState, viewModel)
            }
        }
    }
}
