package com.example.petbionic

import android.os.Bundle
import android.view.View
import android.widget.ProgressBar
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import com.github.mikephil.charting.charts.LineChart
import com.github.mikephil.charting.components.XAxis
import com.github.mikephil.charting.data.*
import com.github.mikephil.charting.formatter.ValueFormatter
import com.google.android.material.appbar.MaterialToolbar
import com.google.firebase.firestore.FirebaseFirestore
import kotlin.math.abs
import kotlin.math.sqrt

class SessionDetailActivity : AppCompatActivity() {

    private lateinit var lineChartForce: LineChart
    private lateinit var lineChartAccel: LineChart
    private lateinit var tvSessionTitle: TextView
    private lateinit var tvStats: TextView
    private lateinit var tvGaitAnalysis: TextView
    private lateinit var progressBar: ProgressBar

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_session_detail)

        val toolbar = findViewById<MaterialToolbar>(R.id.toolbarDetail)
        setSupportActionBar(toolbar)
        supportActionBar?.setDisplayHomeAsUpEnabled(true)
        toolbar.setNavigationOnClickListener { finish() }

        tvSessionTitle = findViewById(R.id.tvSessionTitle)
        tvStats        = findViewById(R.id.tvStats)
        tvGaitAnalysis = findViewById(R.id.tvGaitAnalysis)
        progressBar    = findViewById(R.id.progressBar)
        lineChartForce = findViewById(R.id.lineChartForce)
        lineChartAccel = findViewById(R.id.lineChartAccel)

        val sessionId = intent.getStringExtra("sessionId") ?: return
        tvSessionTitle.text = sessionId
        supportActionBar?.title = sessionId

        setupChart(lineChartForce, ContextCompat.getColor(this, R.color.pb_primary))
        setupChart(lineChartAccel, ContextCompat.getColor(this, R.color.pb_secondary))

        loadSessionData(sessionId)
    }

    private fun setupChart(chart: LineChart, color: Int) {
        chart.description.isEnabled = false
        chart.setTouchEnabled(true)
        chart.setPinchZoom(true)
        chart.setDrawGridBackground(false)
        chart.legend.isEnabled = true
        chart.axisRight.isEnabled = false
        chart.xAxis.position = XAxis.XAxisPosition.BOTTOM
        chart.xAxis.granularity = 0.1f
        chart.xAxis.setDrawGridLines(false)
        chart.xAxis.valueFormatter = object : ValueFormatter() {
            override fun getFormattedValue(value: Float) = "${"%.1f".format(value)}s"
        }
    }

    private fun loadSessionData(sessionId: String) {
        progressBar.visibility = View.VISIBLE
        tvStats.text = "Loading..."

        val db = FirebaseFirestore.getInstance()
        db.collection("sessions").document(sessionId)
            .collection("readings")
            .orderBy("t_rel_ms")
            .get()
            .addOnSuccessListener { documents ->
                progressBar.visibility = View.GONE

                val forceEntries = mutableListOf<Entry>()
                val accelEntries = mutableListOf<Entry>()
                val forceValues  = mutableListOf<Float>()
                val accelValues  = mutableListOf<Float>()
                val rollValues   = mutableListOf<Float>()
                val pitchValues  = mutableListOf<Float>()
                val timeValues   = mutableListOf<Float>()

                for (doc in documents) {
                    val tRelMs   = doc.getLong("t_rel_ms") ?: continue
                    val xSec     = tRelMs.toFloat() / 1000f
                    val loadFilt = doc.getDouble("load_cell_filt")?.toFloat() ?: continue
                    val ax       = doc.getDouble("imu_ax")?.toFloat() ?: 0f
                    val ay       = doc.getDouble("imu_ay")?.toFloat() ?: 0f
                    val az       = doc.getDouble("imu_az")?.toFloat() ?: 0f
                    val accelMag = sqrt(ax * ax + ay * ay + az * az)
                    val roll     = doc.getDouble("roll_deg")?.toFloat() ?: 0f
                    val pitch    = doc.getDouble("pitch_deg")?.toFloat() ?: 0f

                    forceEntries.add(Entry(xSec, loadFilt))
                    accelEntries.add(Entry(xSec, accelMag))
                    forceValues.add(loadFilt)
                    accelValues.add(accelMag)
                    rollValues.add(roll)
                    pitchValues.add(pitch)
                    timeValues.add(tRelMs.toFloat())
                }

                if (documents.isEmpty) {
                    tvStats.text = "No data"
                    Toast.makeText(this, "No readings found for this session", Toast.LENGTH_SHORT).show()
                    return@addOnSuccessListener
                }

                displayStats(forceValues, accelValues, rollValues, pitchValues, timeValues)
                displayGaitAnalysis(accelValues, forceValues, timeValues)
                plotChart(lineChartForce, forceEntries, "Load cell (filtered)",
                    ContextCompat.getColor(this, R.color.pb_primary))
                plotChart(lineChartAccel, accelEntries, "Acceleration magnitude",
                    ContextCompat.getColor(this, R.color.pb_secondary))
            }
            .addOnFailureListener {
                progressBar.visibility = View.GONE
                Toast.makeText(this, "Failed to load session data", Toast.LENGTH_SHORT).show()
            }
    }

    private fun displayStats(
        forceValues: List<Float>,
        accelValues: List<Float>,
        rollValues: List<Float>,
        pitchValues: List<Float>,
        timeValues: List<Float>
    ) {
        val avgForce  = forceValues.average().toFloat()
        val maxForce  = forceValues.maxOrNull() ?: 0f
        val avgRoll   = rollValues.average().toFloat()
        val avgPitch  = pitchValues.average().toFloat()
        val durationS = if (timeValues.size >= 2)
            (timeValues.last() - timeValues.first()) / 1000f else 0f

        tvStats.text =
            "${forceValues.size} readings  •  Duration: ${"%.1f".format(durationS)}s\n" +
                    "Avg force: ${"%.1f".format(avgForce)}  •  Max force: ${"%.1f".format(maxForce)}\n" +
                    "Avg roll: ${"%.1f".format(avgRoll)}°  •  Avg pitch: ${"%.1f".format(avgPitch)}°"
    }

    private fun displayGaitAnalysis(
        accelValues: List<Float>,
        forceValues: List<Float>,
        timeValues: List<Float>
    ) {
        val accelMean = accelValues.average().toFloat()
        val threshold = accelMean + 0.05f
        var stepCount = 0
        for (i in 1 until accelValues.size - 1) {
            if (accelValues[i] > threshold &&
                accelValues[i] > accelValues[i - 1] &&
                accelValues[i] > accelValues[i + 1]) stepCount++
        }

        val durationMs = if (timeValues.size >= 2)
            timeValues.last() - timeValues.first() else 0f
        val cadence = if (durationMs > 0)
            (stepCount / (durationMs / 1000f)) * 60f else 0f
        val rms = sqrt(accelValues.map { it * it }.average()).toFloat()
        val forceVariability = if (forceValues.size > 1)
            forceValues.zipWithNext { a, b -> abs(b - a) }.average().toFloat() else 0f

        tvGaitAnalysis.text =
            "👣 Steps detected: $stepCount\n" +
                    "⏱ Cadence: ${"%.0f".format(cadence)} steps/min\n" +
                    "📊 Activity RMS: ${"%.1f".format(rms)}\n" +
                    "〰️ Force variability: ${"%.1f".format(forceVariability)}"
    }

    private fun plotChart(chart: LineChart, entries: List<Entry>, label: String, color: Int) {
        if (entries.isEmpty()) return
        val ds = LineDataSet(entries, label).apply {
            this.color = color
            setCircleColor(color)
            lineWidth = 1.8f
            circleRadius = 0f
            setDrawCircles(false)
            setDrawValues(false)
            mode = LineDataSet.Mode.CUBIC_BEZIER
        }
        chart.data = LineData(ds)
        chart.invalidate()
    }
}