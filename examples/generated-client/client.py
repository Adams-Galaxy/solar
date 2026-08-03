"""Shape of application code using a generated Solar package."""

from robot_fixture_solar import Robot, models


async def operate(robot: Robot) -> None:
    gain = await robot.parameters.drive.kp.get()
    await robot.parameters.drive.kp.set(min(gain + 0.1, 10.0))
    await robot.data.system.state.set(models.RobotState(enabled=True))
    response = await robot.actions.system.ping()
    print(f"remote sequence: {response.sequence}")
