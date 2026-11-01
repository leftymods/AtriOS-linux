package org.atrios.companion

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.lifecycle.viewmodel.compose.viewModel
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
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
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
                    Column {
                        Text("AtriOS Station", fontWeight = FontWeight.Bold)
                        Text(
                            text = if (uiState.isConnected) "Connected (${uiState.hostIp})" else "Disconnected",
                            style = MaterialTheme.typography.bodySmall,
                            color = if (uiState.isConnected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.error
                        )
                    }
                },
                actions = {
                    IconButton(onClick = { viewModel.refreshStatus() }) {
                        Icon(Icons.Default.Refresh, contentDescription = "Refresh")
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
