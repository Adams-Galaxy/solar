from robot_fixture_solar import Robot, models


async def exercise(robot: Robot) -> None:
    state: models.RobotState = await robot.data.system.state.get()
    await robot.data.system.state.set(models.RobotState(enabled=not state.enabled))
    gain: float = await robot.parameters.drive.kp.get()
    await robot.parameters.drive.kp.set(gain + 0.1)
    response: models.PingResponse = await robot.actions.system.ping()
    command = models.DriveCommand(
        throttle=0.5,
        differential=0.0,
        mode=models.OperatingMode.MANUAL,
    )
    async with robot.streams.drive.command.open(frequency=50) as producer:
        await producer.send(command)
    _ = response
