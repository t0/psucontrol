/*
 * PSU Controller UI
 */


const TelemetryLog = []; // stores telemetry
const EventLog = [];     // stores events (faults)
let lastFaults = [];
const MAX_POINTS = 100;  // Max points to show on charts

// Change these, if necessary
const SAMPLING_THRESHOLD = {
    vin: 0.1,  // Voltage change threshold (V)
    vout: 0.05, // Output voltage change threshold (V)
    iout: 0.05, // Output current change threshold (A)
    temp: 0.5,  // Temperature change threshold (°C)
	oringtemp: 0.5, // Oring temperature change threshold (°C)
	outlettemp: 0.5, // Outlet temperature change threshold (°C)
    fan: 50     // Fan speed change threshold (RPM)
};

let lastSampledData = {
    vin: null,
    vout: null,
    iout: null,
    temp: null,
	oringtemp: null,
	outlettemp: null,
    fan_rpm: null
};

function faultsChanged(newFaults) {
    return JSON.stringify(newFaults) !== JSON.stringify(lastFaults);
}

async function resetFaults() {
	console.log("Clearing PSU faults and warnings...");
    await fetch("/psu-clear-faults", { method: "POST" });
	if (document.getElementById("psu_status").innerHTML === "ON") {
		setTimeout(fetchPsuTelemetry, 1000); // Refresh telemetry to update faults. THIS DOESN'T SEEM TO WORK AS DESIRED :(
	}
}

async function fetchUptime()
{
	try {
		const response = await fetch("/uptime");
		if (!response.ok) {
			throw new Error(`Response status: ${response.status}`);
		}

		const text = await response.text();
		const uptime = document.getElementById("uptime");
		uptime.innerHTML = "Uptime: " + text
	} catch (error) {
		console.error(error.message);
	}
}

function updatePsuFaults(faultData) {
    const faultsDiv = document.getElementById("psu_faults");

    if (!faultData || faultData.length === 0) {
        faultsDiv.innerHTML = "<p>No faults detected.</p>";
        faultsDiv.style.color = "green";
        return;
    }

    // Build list of fault messages
    const html = "<ul>" + faultData.map(f => `<li>${f}</li>`).join("") + "</ul>";
    faultsDiv.innerHTML = html;
    faultsDiv.style.color = "#b00000"; // Red for errors
}

async function fetchPsuTelemetry() {
    try {
        const response = await fetch("/psu");
        if (!response.ok) throw new Error(`Response status: ${response.status}`);

        const data = await response.json();

		// Compare with last sampled data
		const now = new Date().toLocaleTimeString();
        const sampled = [];

		document.getElementById("vin").innerHTML = data.vin;
		document.getElementById("vout").innerHTML = data.vout;
		document.getElementById("iout").innerHTML = data.iout;
		document.getElementById("pout").innerHTML = (data.vout * data.iout).toFixed(2);
		document.getElementById("inlet_temp").innerHTML = data.temp_inlet;
		document.getElementById("oring_temp").innerHTML = data.temp_oring;
		document.getElementById("outlet_temp").innerHTML = data.temp_outlet;
		document.getElementById("fan_rpm").innerHTML = data.fan_rpm;
		document.getElementById("psu_status").innerHTML = data.output_on ? "ON" : "OFF";
		document.getElementById("psu_status").style.color = data.output_on ? "green" : "red";
		
		updatePsuFaults(data.faults);	
		updateCharts(data);


		const isFirstSample = lastSampledData.vin === null;

		if (isFirstSample) {
			Object.assign(lastSampledData, {
				vin: data.vin,
				vout: data.vout,
				iout: data.iout,
				temp_inlet: data.temp_inlet,
				temp_oring: data.temp_oring,
				temp_outlet: data.temp_outlet,
				fan_rpm: data.fan_rpm
			});

			TelemetryLog.push({
				timestamp: now,
				vin: data.vin,
				vout: data.vout,
				iout: data.iout,
				temp_inlet: data.temp_inlet,
				temp_oring: data.temp_oring,
				temp_outlet: data.temp_outlet,
				fan_rpm: data.fan_rpm,
				psu_status: data.output_on ? 1 : 0
			});

			if (faultsChanged(data.faults)) {
				EventLog.push({
					timestamp: now,
					fault: data.faults
				});
				lastFaults = [...data.faults];
			}

			return;
		}


        let shouldSample = false;

		// Check for significant changes
        if (Math.abs(data.vin - lastSampledData.vin) > SAMPLING_THRESHOLD.vin) {
            sampled.push({ vin: data.vin });
            lastSampledData.vin = data.vin;
            shouldSample = true;
        }

        else if (Math.abs(data.vout - lastSampledData.vout) > SAMPLING_THRESHOLD.vout) {
            sampled.push({ vout: data.vout });
            lastSampledData.vout = data.vout;
            shouldSample = true;
        }

        else if (Math.abs(data.iout - lastSampledData.iout) > SAMPLING_THRESHOLD.iout) {
            sampled.push({ iout: data.iout });
            lastSampledData.iout = data.iout;
            shouldSample = true;
        }

        else if (Math.abs(data.temp_inlet - lastSampledData.temp_inlet) > SAMPLING_THRESHOLD.temp_inlet) {
            sampled.push({ temp_inlet: data.temp_inlet });
            lastSampledData.temp_inlet = data.temp_inlet;
            shouldSample = true;
        }

		else if (Math.abs(data.temp_oring - lastSampledData.temp_oring) > SAMPLING_THRESHOLD.temp_oring) {
			sampled.push({ temp_oring: data.temp_oring });
			lastSampledData.temp_oring = data.temp_oring;
			shouldSample = true;
		}

		else if (Math.abs(data.temp_outlet - lastSampledData.temp_outlet) > SAMPLING_THRESHOLD.temp_outlet) {
			sampled.push({ temp_outlet: data.temp_outlet });
			lastSampledData.temp_outlet = data.temp_outlet;
			shouldSample = true;
		}

        else if (Math.abs(data.fan_rpm - lastSampledData.fan_rpm) > SAMPLING_THRESHOLD.fan) {
            sampled.push({ fan_rpm: data.fan_rpm });
            lastSampledData.fan_rpm = data.fan_rpm;
            shouldSample = true;
		}

        // Only update if there's a significant change
        if (shouldSample) {
			const fullPoint = {
				timestamp: now,
				vin: data.vin,
				vout: data.vout,
				iout: data.iout,
				temp_inlet: data.temp_inlet,
				temp_oring: data.temp_oring,
				temp_outlet: data.temp_outlet,
				fan_rpm: data.fan_rpm,
				psu_status: data.output_on ? 1 : 0
			};

			// console.log("Telemetry sampled:", fullPoint);
			TelemetryLog.push(fullPoint);
		}
		

        // // store full telemetry for logging
        // TelemetryLog.push({
        //     timestamp: new Date().toISOString(),
        //     vin: data.vin,
        //     vout: data.vout,
        //     iout: data.iout,
        //     temp_inlet: data.temp_inlet,
        //     temp_oring: data.temp_oring,
        //     temp_outlet: data.temp_outlet,
        //     fan_rpm: data.fan_rpm,
        //     psu_status: data.output_on ? "ON" : "OFF"
        // });

    } catch (error) {
        console.error(error.message);
    }
}

async function setPsuOutput(state)
{
	try {
		const payload = JSON.stringify({"output_state" : state});

		const response = await fetch("/psu-control", {method : "POST", body : payload});
		if (!response.ok) {
			throw new Error(`Response status: ${response.status}`);
		}

		/* Refresh telemetry after changing state */
		setTimeout(fetchPsuTelemetry, 200);
	} catch (error) {
		console.error(error.message);
	}
}

window.addEventListener("DOMContentLoaded", (ev) => {
	/* Fetch uptime and PSU telemetry once per second */
	setInterval(fetchUptime, 1000);
	setInterval(fetchPsuTelemetry, 1000);

	/* Initial fetch */
	fetchPsuTelemetry();

	/* PSU output control buttons */
	const psu_on_btn = document.getElementById("psu_on");
	psu_on_btn.addEventListener("click", (event) => {
		console.log("PSU output ON clicked");
		setPsuOutput(true);
	})

	const psu_off_btn = document.getElementById("psu_off");
	const modal = document.getElementById("confirmModal");
	const confirmYes = document.getElementById("confirmYes");
	const confirmNo = document.getElementById("confirmNo");

	psu_off_btn.addEventListener("click", (event) => {
		if (document.getElementById("psu_status").innerHTML === "OFF") {
			return; // if PSU is already off, do nothing
		}
		modal.style.display = "block";
		confirmYes.focus();
	}); // Doing it this way because `confirm()` stops telemetry updates. Needed to get crafty!

	confirmYes.addEventListener("click", () => { 
		console.log("PSU output OFF confirmed");
		setPsuOutput(false);
		modal.style.display = "none";
	});
	confirmYes.addEventListener("keydown", (e) => {
		if (e.key === "ArrowLeft" || e.key === "ArrowRight") confirmNo.focus();
	});

	confirmNo.addEventListener("click", () => { 
		console.log("PSU output OFF cancelled");
		modal.style.display = "none"; 
	});
	confirmNo.addEventListener("keydown", (e) => {
		if (e.key === "ArrowLeft" || e.key === "ArrowRight") confirmYes.focus();
	});

	initCharts();
	
	const loggerBtn = document.getElementById("logger_btn");
	loggerBtn.addEventListener("click", () => {
		if (TelemetryLog.length === 0) {
			alert("No telemetry data to log yet!");
			return;
		}

		// telemetry csv
		const headers = Object.keys(TelemetryLog[0]).join(",");
		const rows = TelemetryLog.map(d => Object.values(d).join(","));
		const csvContent = [headers, ...rows].join("\n");
		const telemetryBlob = new Blob([csvContent], { type: "text/csv" });
		const telemetryURL = URL.createObjectURL(telemetryBlob);

		const datenow = new Date().toLocaleDateString().replace(/\//g, '-');
		const timnow = new Date().toLocaleTimeString().replace(/:/g, '-');

		const a = document.createElement("a");
		a.href = telemetryURL;
		a.download = `psu_telemetry_${datenow}_${timnow}.csv`;
		a.click();

		// event log
		const eventContent = EventLog.map(e => `${e.timestamp} - ${e.fault.join(", ")}`).join("\n");
		const eventBlob = new Blob([eventContent], { type: "text/plain" });
		const eventURL = URL.createObjectURL(eventBlob);

		const b = document.createElement("a");
		b.href = eventURL;
		b.download = `psu_EventLog_${datenow}_${timnow}.log`;
		b.click();

		// Revoke URLs to free memory
		URL.revokeObjectURL(telemetryURL);
		URL.revokeObjectURL(eventURL);
	});
});


/* ===== Chart Setup ===== */

const charts = {}; // store chart objects
const chartData = {}; // store chart data arrays

function initCharts() {
	const metrics = [
		{ id: "vinChart", label: "Input Voltage (V)", color: "rgb(75,192,192)" },
		{ id: "voutChart", label: "Output Voltage (V)", color: "rgb(54,162,235)" },
		{ id: "ioutChart", label: "Output Current (A)", color: "rgb(255,99,132)" },
		{ id: "poutChart", label: "Output Power (W)", color: "rgb(255,159,64)" },
		{ id: "tempChart", label: "Inlet Temperature (C)", color: "rgb(255,206,86)" },
		{ id: "oringTempChart", label: "O-ring Temperature (C)", color: "rgb(153,102,255)" },
		{ id: "outletTempChart", label: "Outlet Temperature (C)", color: "rgb(255,102,255)" },
		{ id: "fanChart", label: "Fan Speed (RPM)", color: "rgb(153,102,255)" },
	];

	metrics.forEach(metric => {
		// Create canvas dynamically if not in HTML
		if (!document.getElementById(metric.id)) {
			const canvas = document.createElement("canvas");
			canvas.id = metric.id;
			canvas.width = 400;
			canvas.height = 200;
			document.body.appendChild(document.createElement("hr"));
			document.body.appendChild(canvas);
		}

		chartData[metric.id] = { labels: [], data: [] };

		const ctx = document.getElementById(metric.id).getContext("2d");
		charts[metric.id] = new Chart(ctx, {
			type: "line",
			data: {
				labels: chartData[metric.id].labels,
				datasets: [{
					label: metric.label,
					borderColor: metric.color,
					backgroundColor: "rgba(0,0,0,0)",
					data: chartData[metric.id].data,
					tension: 0.2
				}]
			},
			options: {
				responsive: true,
				animation: false,
				scales: {
					x: { title: { display: true, text: "Time" } },
					y: { beginAtZero: false }
				}
			}
		});
	});
}

function updateCharts(data) {
	const now = new Date().toLocaleTimeString();

	const values = {
		vinChart: data.vin,
		voutChart: data.vout,
		ioutChart: data.iout,
		poutChart: (data.vout * data.iout).toFixed(2),
		tempChart: data.temp_inlet,
		oringTempChart: data.temp_oring,
		outletTempChart: data.temp_outlet,
		fanChart: data.fan_rpm
	};

	for (const [id, value] of Object.entries(values)) {
		const chartObj = charts[id];
		const chartArray = chartData[id];

		chartArray.labels.push(now);
		chartArray.data.push(value);

		if (chartArray.labels.length > MAX_POINTS) {
			chartArray.labels.shift();
			chartArray.data.shift();
		}

		chartObj.update();
	}
}