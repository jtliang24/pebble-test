var hours = [];
for (var h = 0; h < 24; h++) {
  hours.push({ label: (h < 10 ? '0' : '') + h + ':00', value: String(h) });
}

module.exports = [
  {
    type: 'heading',
    defaultValue: 'Standup Reminder'
  },
  {
    type: 'text',
    defaultValue: 'Vibrates after you have been sitting for a while. Walking resets the countdown.'
  },
  {
    type: 'section',
    items: [
      {
        type: 'heading',
        defaultValue: 'Reminders'
      },
      {
        type: 'toggle',
        messageKey: 'ENABLED',
        label: 'Enabled',
        defaultValue: true
      },
      {
        type: 'slider',
        messageKey: 'INTERVAL_MINUTES',
        label: 'Remind after sitting for (minutes)',
        defaultValue: 30,
        min: 5,
        max: 120,
        step: 5
      },
      {
        type: 'slider',
        messageKey: 'ACTIVITY_THRESHOLD',
        label: 'Steps within 2 minutes that count as moving',
        defaultValue: 50,
        min: 10,
        max: 200,
        step: 5
      }
    ]
  },
  {
    type: 'section',
    items: [
      {
        type: 'heading',
        defaultValue: 'Active hours'
      },
      {
        type: 'select',
        messageKey: 'ACTIVE_START_HOUR',
        label: 'Start',
        defaultValue: '9',
        options: hours
      },
      {
        type: 'select',
        messageKey: 'ACTIVE_END_HOUR',
        label: 'End',
        defaultValue: '18',
        options: hours
      },
      {
        type: 'text',
        defaultValue: 'Set start and end to the same hour to get reminders all day.'
      }
    ]
  },
  {
    type: 'submit',
    defaultValue: 'Save Settings'
  }
];
