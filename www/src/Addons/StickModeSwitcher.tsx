import * as yup from 'yup';
import { FormCheck, Row } from 'react-bootstrap';

import Section from '../Components/Section';
import FormControl from '../Components/FormControl';
import { AddonPropTypes } from '../Pages/AddonsConfigPage';

import useBoardDefinition from '../Store/useBoardDefinitionStore';

export const stickModeSwitcherScheme = {
	StickModeSwitcherEnabled: yup
		.number()
		.required()
		.label('Stick Mode Switcher Add-On Enabled'),
	stickModeSwitcherPin: yup
		.number()
		.label('Mode Cycle Button GPIO')
		.validatePinWhenValue('StickModeSwitcherEnabled'),
};

export const stickModeSwitcherState = {
	StickModeSwitcherEnabled: 0,
	stickModeSwitcherPin: 18,
};

const StickModeSwitcher = ({
	values,
	errors,
	handleChange,
	handleCheckbox,
}: AddonPropTypes) => {
	const { boardDefinition } = useBoardDefinition();
	return (
		<Section title="Stick Mode Switcher">
			<div
				id="StickModeSwitcherOptions"
				hidden={!values.StickModeSwitcherEnabled}
			>
				<Row className="mb-3">
					<FormControl
						type="number"
						label="Mode Cycle Button GPIO"
						name="stickModeSwitcherPin"
						className="form-control-sm"
						groupClassName="col-sm-3 mb-3"
						value={values.stickModeSwitcherPin}
						error={errors.stickModeSwitcherPin}
						isInvalid={Boolean(errors.stickModeSwitcherPin)}
						onChange={handleChange}
						min={-1}
						max={boardDefinition.maxPin}
					/>
				</Row>
				<p className="text-muted small">
					Each press of the button cycles the stick between: Left Analog, Right
					Analog, Keyboard WASD. Stick pins / calibration / deadzone / forced
					circularity settings are reused from the native Analog add-on (Analog
					Stick 1). WASD output requires the device input mode to be Keyboard.
				</p>
			</div>
			<FormCheck
				label="Enabled"
				type="switch"
				id="StickModeSwitcherButton"
				reverse
				isInvalid={false}
				checked={Boolean(values.StickModeSwitcherEnabled)}
				onChange={(e) => {
					handleCheckbox('StickModeSwitcherEnabled');
					handleChange(e);
				}}
			/>
		</Section>
	);
};

export default StickModeSwitcher;
