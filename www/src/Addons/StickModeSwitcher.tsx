import * as yup from 'yup';
import { FormCheck, Row } from 'react-bootstrap';

import Section from '../Components/Section';
import FormControl from '../Components/FormControl';
import FormSelect from '../Components/FormSelect';
import { KEY_CODES } from '../Data/Keyboard';
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
	wasdKeyUp: yup.number().required().label('WASD Up Key'),
	wasdKeyDown: yup.number().required().label('WASD Down Key'),
	wasdKeyLeft: yup.number().required().label('WASD Left Key'),
	wasdKeyRight: yup.number().required().label('WASD Right Key'),
};

export const stickModeSwitcherState = {
	StickModeSwitcherEnabled: 0,
	stickModeSwitcherPin: 18,
	// Default keys: W / S / A / D (USB HID Usage IDs, decimal)
	wasdKeyUp: 0x1a,
	wasdKeyDown: 0x16,
	wasdKeyLeft: 0x04,
	wasdKeyRight: 0x07,
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

				<Row className="mb-3">
					<p className="mb-1">
						Keyboard WASD Mode Keys{' '}
						<span className="text-muted small">
							(push the stick in each direction to send the selected key)
						</span>
					</p>
					<FormSelect
						label="Stick Up key"
						name="wasdKeyUp"
						className="form-select-sm"
						groupClassName="col-sm-3 mb-3"
						value={values.wasdKeyUp}
						error={errors.wasdKeyUp}
						onChange={handleChange}
					>
						{KEY_CODES.map((o, i) => (
							<option key={`wasd-up-${i}`} value={o.value}>
								{o.label}
							</option>
						))}
					</FormSelect>
					<FormSelect
						label="Stick Down key"
						name="wasdKeyDown"
						className="form-select-sm"
						groupClassName="col-sm-3 mb-3"
						value={values.wasdKeyDown}
						error={errors.wasdKeyDown}
						onChange={handleChange}
					>
						{KEY_CODES.map((o, i) => (
							<option key={`wasd-down-${i}`} value={o.value}>
								{o.label}
							</option>
						))}
					</FormSelect>
					<FormSelect
						label="Stick Left key"
						name="wasdKeyLeft"
						className="form-select-sm"
						groupClassName="col-sm-3 mb-3"
						value={values.wasdKeyLeft}
						error={errors.wasdKeyLeft}
						onChange={handleChange}
					>
						{KEY_CODES.map((o, i) => (
							<option key={`wasd-left-${i}`} value={o.value}>
								{o.label}
							</option>
						))}
					</FormSelect>
					<FormSelect
						label="Stick Right key"
						name="wasdKeyRight"
						className="form-select-sm"
						groupClassName="col-sm-3 mb-3"
						value={values.wasdKeyRight}
						error={errors.wasdKeyRight}
						onChange={handleChange}
					>
						{KEY_CODES.map((o, i) => (
							<option key={`wasd-right-${i}`} value={o.value}>
								{o.label}
							</option>
						))}
					</FormSelect>
				</Row>

				<p className="text-muted small">
					Each press of the button cycles the stick between: Left Analog, Right
					Analog, Keyboard WASD. Stick pins / calibration / deadzone / forced
					circularity settings are reused from the native Analog add-on (Analog
					Stick 1). WASD output requires the device input mode to be Keyboard.
					Keys are picked from the full USB HID keyboard list (letters, numbers,
					F1-F24, arrows, numpad, modifiers, media keys); pick "None" to make a
					direction send no key.
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
