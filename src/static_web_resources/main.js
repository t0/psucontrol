/*
 * PSU Controller UI
 */

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

async function fetchPsuTelemetry()
{
	try {
		const response = await fetch("/psu");
		if (!response.ok) {
			throw new Error(`Response status: ${response.status}`);
		}

		const data = await response.json();

		document.getElementById("vin").innerHTML = data.vin;
		document.getElementById("vout").innerHTML = data.vout;
		document.getElementById("iout").innerHTML = data.iout;
		document.getElementById("temp").innerHTML = data.temp;
		document.getElementById("fan_rpm").innerHTML = data.fan_rpm;
		document.getElementById("psu_status").innerHTML = data.output_on ? "ON" : "OFF";
		document.getElementById("psu_status").style.color = data.output_on ? "green" : "red";
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
	psu_off_btn.addEventListener("click", (event) => {
		console.log("PSU output OFF clicked");
		setPsuOutput(false);
	})
})
